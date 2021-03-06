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

#include "network.h"
#include "logger.h"
#include "settings.h"

Network::Network(Daemon *daemon) : QObject(0)
{
	// Initialization
	d = daemon;
	lg = Logger::get();
	sg = daemon->getSettings();
	// Connections
	// Threading
#ifdef NETWORK_THREAD
	QThread *t = new QThread(daemon);
	moveToThread(t);
	connect(t, &QThread::started, this, &Network::init);
	connect(this, &Network::destroyed, t, &QThread::quit);
	connect(t, &QThread::finished, t, &QThread::deleteLater);
	t->start();
#else
	init();
#endif
}

Network::~Network() {
	// Forcibly close open connections (should have been gracefully handled in
	// shutdown() if possible)
	server->close();
	// These send disconnect signals - this needs to be handled.
	QHash<QTcpSocket*, NetDev*>::const_iterator it;
	for (it = cons.constBegin(); it != cons.constEnd(); ++it) {
		if ((*it)->socket()->canReadLine()) {
			log("Can read data");
		}
	}
}

void Network::init() {
	// Initializations
	server = new QTcpServer(this);
	// TODO: add a QNetworkAccessManager and related stuff so Modules can use the high-level APIs
	// Connections
	//connect(server, &QTcpServer::acceptError, this, &Network::handleNetworkError);
	//connect(server, &QTcpServer::newConnection, this, &Network::handleConnection);
	int port = sg->v("GUIPort", SG_NETWORK).toInt();
	// Filter listening addresses
	QHostAddress a;
	if (sg->v("AllowExternal", SG_NETWORK).toBool())
		a = QHostAddress::Any;
	else {
		if (sg->v("UseIPv6Localhost", SG_NETWORK).toBool())
			a = QHostAddress::LocalHostIPv6;
		else a = QHostAddress::LocalHost;
	}
	if ( ! server->listen(a, port)) {
		// TODO:  Should be alert
		log(tr("Server creation failed with error '%1'.  This is likely "
				 "because another DDX daemon is already running on this machine.")
			  .arg(server->errorString()));
		d->quit(E_TCP_SERVER_FAILED);
		return;
	}
}

void Network::shutdown() {
	log(tr("Closing network connections"));
	// TODO:  Close all connections gracefully without using event loop
	// This function must be thread-safe with regards to being called by
	// the daemon
	server->close();
}

void Network::handleData() {
	// TODO:  Add buffer size checks; if they exceed value (setting),
	// clear the buffer and send an error
	QHash<QTcpSocket*, NetDev*>::const_iterator it;
	for (it = cons.constBegin(); it != cons.constEnd(); ++it) {
		if ((*it)->socket()->canReadLine()) {
			log("Can read data");
		}
	}
	/*for (int i = 0; i < ur_sockets.size(); i++) {
		if (ur_sockets.at(i)->canReadLine()) {
			QString line = QString(ur_sockets.at(i)->readLine()).trimmed();
			log(QString("Device said '%1'").arg(line));
			if (QString::compare(line, "exit") == 0) {
				metaObject()->invokeMethod(d, "quit", Qt::QueuedConnection);
				return;
			}
		}
	}*/
	
	// Check for buffer overflow
	// Ignore consecutive newlines
}

void Network::handleConnection(QTcpSocket *socket) {
	QTcpSocket *s;
	while ((s = server->nextPendingConnection())) {
		if (s->state() != QAbstractSocket::ConnectedState) {
			log(tr("Pending connection was invalid"));
			s->deleteLater();
			continue;
		}
		// Determine basic information about the connection itself
		bool usingIPv6 = s->peerAddress().protocol() == QAbstractSocket::IPv6Protocol;
		QHostAddress localhost = usingIPv6 ? QHostAddress::LocalHostIPv6 : QHostAddress::LocalHost;
		bool isLocal = s->peerAddress() == localhost;
		
		//QTimer::singleShot(REGISTRATION_TIMEOUT_TIMER, Qt::VeryCoarseTimer, this, &RemDev::registerTimeout);
		
		// QTcpServer::error is overloaded, so we need to use this nasty thing
		//connect(s, static_cast<void(QTcpSocket::*)(QAbstractSocket::SocketError)>(&QTcpSocket::error),
		//		this, &Network::handleNetworkError);
		//connect(s, &QTcpSocket::disconnected, this, &Network::handleDisconnection);
		connect(s, &QTcpSocket::readyRead, this, &Network::handleData);
		NetDev *dev = new NetDev(d, true);
		cons.insert(s, dev);
		s->setParent(this);
		s->setSocketOption(QAbstractSocket::LowDelayOption, 1);  // Disable Nagel's algorithm
		if ( ! isLocal) s->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
	}
}

void Network::handleDisconnection() {
	// REMOVED
}

void Network::handleNetworkError(QAbstractSocket::SocketError error) {
	// REMOVED
}

void Network::log(const QString msg) const {
	QString out("network: ");
	out.append(msg);
	lg->log(msg);
}

QByteArray Network::generateCid(const QByteArray &base) const {
	int i = 0;
	QByteArray cid;
	do {
		i++;
		cid = base;
		cid.append(QByteArray::number(i));
	}
	while (false);
	// TODO!!!!!!!!!!
	//while (cons.contains(cid));
	return cid;
}
