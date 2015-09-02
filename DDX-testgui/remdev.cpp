/******************************************************************************
 *                         DATA DISPLAY APPLICATION X                         *
 *                            2B TECHNOLOGIES, INC.                           *
 *                                                                            *
 * The DDX is free software: you can redistribute it and/or modify it under   *
 * the terms of the GNU General Public License as published by the Free       *
 * Software Foundation, either version 3 of the License, or (at your option)  *
 * any later version.  The DDX is distributed in the hope that it will be     *
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of     *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General  *
 * Public License for more details.  You should have received a copy of the   *
 * GNU General Public License along with the DDX.  If not, see                *
 * <http://www.gnu.org/licenses/>.                                            *
 *                                                                            *
 *  For more information about the DDX, check out the 2B website or GitHub:   *
 *       <http://twobtech.com/DDX>       <https://github.com/2BTech/DDX>      *
 ******************************************************************************/

#include "remdev.h"
#include "devmgr.h"

#define RAPIDJSON_IO
#include "rapidjson_using.h"

RemDev::RemDev(DevMgr *dm, bool inbound) :
		QObject(0), req_id_lock(QMutex::Recursive) {
	// Initializations
	connectTime = QDateTime::currentMSecsSinceEpoch();
	this->dm = dm;
	lastId = 0;
	pollerRefCount = 0;
	registered = false;
	regState = UnregisteredState;
	closed = false;
	this->inbound = inbound;
	// Add to master device list and get temporary cid
	cid = dm->addDevice(this);
	log(tr("New unregistered device"));
	// Threading
#ifdef REMDEV_THREADS
	QThread *t = new QThread(dm);
	moveToThread(t);
	connect(t, &QThread::started, this, &RemDev::init);
	connect(this, &RemDev::destroyed, t, &QThread::quit);
	connect(t, &QThread::finished, t, &QThread::deleteLater);
	t->start();
#else
	init();
#endif
}

RemDev::~RemDev() {
	if ( ! closed) close(UnknownReason);
}

int RemDev::sendRequest(QObject *self, const char *handler, const char *method, rapidjson::Document *doc,
						rapidjson::Value *params, qint64 timeout) {
	LocalId id;
	RequestRef ref(self, handler, method, timeout);
	req_id_lock.lock();
	// Check that the connection is still open while we've got the lock
	if (closed) {
		req_id_lock.unlock();
		if (doc) delete doc;
		return -1;
	}
	// Obtain a server-unique ID
	do {
		id = ++lastId;
		if (id == std::numeric_limits<LocalId>::max()) {
			log("ID generator overflow; resetting");
			lastId = 0;
		}
	} while (reqs.contains(id));
	// Insert into request list
	reqs.insert(id, ref);
	req_id_lock.unlock();
	// Build & send request
	if ( ! doc) doc = new Document;
	rapidjson::MemoryPoolAllocator<> &a = doc->GetAllocator();
	prepareDocument(doc, a);
	doc->AddMember("method", Value().SetString(rapidjson::StringRef(method)), a);
	doc->AddMember("id", Value(id), a);
	if (params) doc->AddMember("params", *params, a);
	sendDocument(doc);
	return id;
}

void RemDev::sendResponse(rapidjson::Value &id, rapidjson::Document *doc, rapidjson::Value *result) {
	if (closed) {
		if (doc) delete doc;
		return;
	}
	if ( ! doc) doc = new Document;
	rapidjson::MemoryPoolAllocator<> &a = doc->GetAllocator();
	prepareDocument(doc, a);
	doc->AddMember("id", id, a);
	if (result) doc->AddMember("result", *result, a);
	else doc->AddMember("result", Value(rapidjson::kTrueType), a);
	sendDocument(doc);
}

void RemDev::sendError(rapidjson::Value *id, int code, const QString &msg, rapidjson::Document *doc, rapidjson::Value *data) noexcept {
	if (closed) {
		if (doc) delete doc;
		return;
	}
	if ( ! doc) doc = new Document;
	rapidjson::MemoryPoolAllocator<> &a = doc->GetAllocator();
	Value e(kObjectType);
	{
		Value v(code);
		e.AddMember("code", v, a);
		QByteArray encodedMsg = msg.toUtf8();
		v.SetString(encodedMsg.constData(), encodedMsg.size(), a);
		e.AddMember("message", v, a);
	}
	if (data) e.AddMember("data", *data, a);
	prepareDocument(doc, a);
	doc->AddMember("error", e, a);
	if (id) doc->AddMember("id", *id, a);
	else doc->AddMember("id", Value(rapidjson::kNullType), a);  // Add null if no id present
	sendDocument(doc);
}

void RemDev::sendNotification(const char *method, rapidjson::Document *doc, rapidjson::Value *params) noexcept {
	if (closed) {
		if (doc) delete doc;
		return;
	}
	if ( ! doc) doc = new Document;
	rapidjson::MemoryPoolAllocator<> &a = doc->GetAllocator();
	prepareDocument(doc, a);
	doc->AddMember("method", Value().SetString(rapidjson::StringRef(method)), a);
	if (params) doc->AddMember("params", *params, a);
	sendDocument(doc);
}

void RemDev::close(DisconnectReason reason, bool fromRemote) noexcept {
	emit deviceDisconnected(this, reason, fromRemote);
	req_id_lock.lock();
	closed = true;
	for (RequestHash::ConstIterator it = reqs.constBegin(); it != reqs.constEnd(); ++it)
		simulateError(it.key(), it.value(), E_DEVICE_DISCONNECTED,
					  tr("Device disconnected"));
	req_id_lock.unlock();
	terminate(reason, fromRemote);
	log(tr("Connection closed"));
	dm->removeDevice(this);
	deleteLater();
}

void RemDev::timeoutPoll() noexcept {
	qint64 time = QDateTime::currentMSecsSinceEpoch();
	req_id_lock.lock();
	RequestHash::iterator it = reqs.begin();
	while (it != reqs.end()) {
		if ( ! it->valid(time)) {
			simulateError(it.key(), it.value(), E_REQUEST_TIMEOUT,
						  tr("Request timed out"));
			it = reqs.erase(it);
		}
		else ++it;
	}
	req_id_lock.unlock();
	if ( ! registered && registrationTimeoutTime < time) {
		// TODO: Disconnect for registration timeout
	}
}

void RemDev::init() noexcept {
/*	int registrationPeriod = sg->v("RegistrationPeriod", SG_RPC).toInt();
	if (registrationPeriod < 1) {
		lg->log(tr("User attempted to set a registration period less than 1 second; defaulting"));
		registrationPeriod = sg->reset("RegistrationPeriod", SG_RPC).toInt();
	}
	registrationTimeoutTime = connectTime + (registrationPeriod * 1000);*/
	// Call the subclass init function
	sub_init();
}

void RemDev::handleItem(char *data) noexcept {
	// Parse document
	Document *doc = new Document;
	registered = true;  // TODO:  Remove this
	if (registered) {  // Parsing procedure is more lenient with registered connections
		doc->ParseInsitu(data);
		if (doc->HasParseError()) {
			delete data;
			// TODO
			// Does this leave doc hanging?  We're overwriting a previously parsed doc...
			// It might be a memory leak.  Also of note is that we delete the data in which
			// this document was parsed before we overwrite the doc.
			sendError(0, E_JSON_PARSE, tr("Parse error"), doc);
			return;
		}
	}
	else {  // Unregistered: strict parsing, return if errors
		doc->ParseInsitu<rapidjson::kParseValidateEncodingFlag |
						 rapidjson::kParseIterativeFlag>
				(data);
		// Assume this is a fake connection; return nothing if there's a parse error
		if (doc->HasParseError()) {
			delete data;
			delete doc;
			return;
		}
		handleRegistration(doc, data);
		return;
	}
	if (doc->IsArray()) {
		// TODO
		// Does this leave doc hanging?  We're overwriting a previously parsed doc...
		// It might be a memory leak.  Also of note is that we delete the data in which
		// this document was parsed before we overwrite the doc.
		sendError(0, E_NO_BATCH, tr("Batch not supported"), doc);
		delete data;
		return;
	}
	if ( ! doc->IsObject()) {
		// TODO
		// Does this leave doc hanging?  We're overwriting a previously parsed doc...
		// It might be a memory leak.  Also of note is that we delete the data in which
		// this document was parsed before we overwrite the doc.
		sendError(0, E_JSON_PARSE, tr("Invalid JSON"), doc);
		delete data;
		return;
	}
	if (doc->HasMember("method")) {
		handleRequest_Notif(doc, data);
		return;
	}
	if (doc->HasMember("id")) {
		handleResponse(doc, data);
		return;
	}
	delete data;
	delete doc;
}

void RemDev::log(const QString &msg, bool isAlert) const noexcept {
	(void) isAlert;
	QString out = QString::fromUtf8(cid);
	out.append(": ").append(msg);
	emit postToLogArea(out);
}

void RemDev::sendDocument(rapidjson::Document *doc) {
	StringBuffer *buffer = new StringBuffer;
	Writer<StringBuffer> writer(*buffer);
	doc->Accept(writer);
	delete doc;
	writeItem(buffer);
}

void RemDev::handleRequest_Notif(rapidjson::Document *doc, char *buffer) {
	//Document *doc;
	//if (doc->HasMember())
	delete doc;
	delete buffer;
}

void RemDev::handleResponse(rapidjson::Document *doc, char *buffer) {
	// Find and type-check all elements efficiently
	Value *idVal = 0, *mainVal = 0;
	bool wasSuccessful;
	// Breaking out of this loop while idVal or mainVal is 0 constitutes an error,
	// whereas finishing with both pointers set allows proper handling
	for (Value::ConstMemberIterator it = doc->MemberBegin(); it != doc->MemberEnd(); ++it) {
		const char *name = it->name.GetString();
		Value &value = (Value &) it->value;
		if (strcmp("jsonrpc", name) == 0) continue;
		if (strcmp("id", name) == 0) {
			if ( ! (value.IsInt() || value.IsNull())) {
				idVal = 0;  // Force an error (possible attack with two ID values otherwise)
				break;
			}
			idVal = &value;
			continue;
		}
		if ( ! mainVal) {
			// Check for successful result
			if (strcmp("result", name) == 0) {
				mainVal = &value;
				wasSuccessful = true;
				continue;
			}
			// Check for error
			if (strcmp("error", name) == 0) {
				// Verify that the error object meets basic requirements
				if ( ! value.IsObject()) break;
				Value::ConstMemberIterator eIt = value.FindMember("code");
				if (eIt == value.MemberEnd() || ! eIt->value.IsInt()) break;
				eIt = value.FindMember("message");
				if (eIt == value.MemberEnd() || ! eIt->value.IsString()) break;
				// Save error
				mainVal = &value;
				wasSuccessful = false;
				continue;
			}
		}
		// There was an unknown member
		break;
	}
	if (idVal && mainVal) {  // If everything was found successfully...
		// Handle null errors
		if (idVal->IsNull()) {
			if (wasSuccessful) log(tr("Received successful null-ID response; ignoring"));
			else logError(mainVal);
			delete doc;
			delete buffer;
			return;
		}
		// Handle normal response
		int id = idVal->GetInt();
		req_id_lock.lock();
		RequestRef &&req = reqs.take(id);  // Will be invalid if the id does not exist
		req_id_lock.unlock();
		if (req.valid(0)) {  // If the request (still) exists...
			// Log errors
			if ( ! wasSuccessful) logError(mainVal, req.method);
			// Call handler
			Response *res = new Response(wasSuccessful, id, req.method, doc, buffer, mainVal);
			metaObject()->invokeMethod(req.handlerObj, req.handlerFn,
									   Qt::QueuedConnection, Q_ARG(RemDev::Response*, res));
		}
		else {  // If the request does not exist...
			delete buffer;
			// TODO
			// Does this leave doc hanging?  We're overwriting a previously parsed doc...
			// It might be a memory leak.  Also of note is that we delete the data in which
			// this document was parsed before we overwrite the doc.
			sendError(0, E_INVALID_RESPONSE, tr("Invalid response"), doc, idVal);
			// TODO:  also check that idVal is deleted when doc is deleted
		}
	}
	else {
		delete buffer;
		// TODO
		// Does this leave doc hanging?  We're overwriting a previously parsed doc...
		// It might be a memory leak.  Also of note is that we delete the data in which
		// this document was parsed before we overwrite the doc.
		sendError(0, E_JSON_PARSE, tr("Invalid JSON"), doc);
	}
}

void RemDev::handleRegistration(rapidjson::Document *doc, char *buffer) {
	// If this is not a register request or response, return without error
	if ((regState & RegSentFlag) && doc->HasMember("result")) {  // This is a response to our registration
		
	}
	/*if (inbound && QString::compare(doc.value("method").toString(), "register")) return;
	if ( ! inbound && ! doc.contains("result")) return;
	QJsonValue id = doc.value("id");
	// Check minimum version
	QString sent = doc.value("DDX_version").toString();
	QString check = sg->v("MinVersion", SG_RPC).toString();
	if (QString::compare(check, "any")) {  // If check is required...
		int vc = Daemon::versionCompare(sent, check);
		if (vc == VERSION_COMPARE_FAILED) {
			if (inbound) sendError(id, E_VERSION_UNREADABLE, tr("Version unreadable"));
			return;
		}
		if (vc < 0) {
			// Send error
		}
	}
	// UPDATE:  I think both devices should independently send register requests to each other.  Why not, right?
	// It would make a lot of this MUCH cleaner...
	// A successful result response and a register request should both be required
	
	
	//log(tr("Now known as %1").arg(
	dropPoller();
	registered = true;
	d->registerDevice(this);*/
}

void RemDev::handleDisconnect(rapidjson::Document *doc, char *buffer) {
	
}

void RemDev::simulateError(int id, const RequestRef &req, int code, const QString &msg) {
	Document *doc = new Document;
	rapidjson::MemoryPoolAllocator<> &a = doc->GetAllocator();
	doc->SetObject();
	{
		Value v(code);
		doc->AddMember("code", v, a);
		QByteArray encodedMsg = msg.toUtf8();
		v.SetString(encodedMsg.constData(), encodedMsg.size(), a);
		doc->AddMember("message", v, a);
	}
	logError(doc, req.method);
	Response *res = new Response(false, id, req.method, doc, 0, doc);
	metaObject()->invokeMethod(req.handlerObj, req.handlerFn,
							   Qt::QueuedConnection, Q_ARG(RemDev::Response*, res));
}

void RemDev::logError(const Value *errorVal, const char *method) const {
	QString logStr;
	if (method)
		logStr = tr("Request to '%1' failed with error %2: %3").arg(QString::fromUtf8(method));
	else
		logStr = tr("Received null error %2: %3");
	const Value &code = errorVal->FindMember("code")->value;
	const Value &message = errorVal->FindMember("message")->value;
	log(logStr.arg(QString::number(code.GetInt()), QString::fromUtf8(message.GetString())));
}

inline void RemDev::prepareDocument(rapidjson::Document *doc, rapidjson::MemoryPoolAllocator<> &a) {
	doc->SetObject();
	doc->AddMember("jsonrpc", "2.0", a);
}

#ifdef QT_DEBUG
void RemDev::printReqs() const {
	Document doc;
	rapidjson::MemoryPoolAllocator<> &a = doc.GetAllocator();
	doc.SetArray();
	req_id_lock.lock();
	for (RequestHash::ConstIterator it = reqs.constBegin(); it != reqs.constEnd(); ++it) {
		Value v(kArrayType);
		v.PushBack(Value(it.key()), a);
		QByteArray time = QDateTime::fromMSecsSinceEpoch(it->timeout_time).toString("HH:mm:ss.zzz").toUtf8();
		Value s;
		s.SetString(time.constData(), a);
		v.PushBack(s, a);
		s.SetString(rapidjson::StringRef(it->handlerFn));
		v.PushBack(s, a);
		if (it->valid(0)) {
			const char *name = it->handlerObj->metaObject()->className();
			s.SetString(rapidjson::StringRef(name));
			v.PushBack(s, a);
			v.PushBack(Value(rapidjson::kTrueType), a);
		}
		else v.PushBack(Value(rapidjson::kFalseType), a);
		doc.PushBack(v, a);
	}
	StringBuffer buffer;
	Writer<StringBuffer> writer(buffer);
	doc.Accept(writer);
	log(buffer.GetString());
	req_id_lock.unlock();
}

QByteArray RemDev::serializeValue(const rapidjson::Value &v) {
	Document doc;
	doc.CopyFrom(v, doc.GetAllocator());
	StringBuffer buffer;
	Writer<StringBuffer> writer(buffer);
	doc.Accept(writer);
	QByteArray array(buffer.GetString());
	Q_ASSERT(buffer.GetSize() == (uint) array.size());
	return array;
}
#endif  // QT_DEBUG
