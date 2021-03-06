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

#include "examplemodule.h"
#include "../rapidjson_using.h"

ExampleModule::~ExampleModule() {
	alert("ExampleModule::~ExampleModule()");
}

void ExampleModule::init(rapidjson::Value &config) {
	(void) config;
	// TODO:  rapidjson
	/*echo = config.value("Echo_string").toString();
	if (config.value("Fail_on_init").toBool())
		terminate("Simulating a failure as requested by settings");*/
	alert("ExampleModule::init()");
}

void ExampleModule::process() {
	alert("ExampleModule::process()");
	alert(echo);
}

rapidjson::Value ExampleModule::publishSettings(rapidjson::MemoryPoolAllocator<> &a) const {
	alert("ExampleModule::publishSettings()");
	// TODO:  rapidjson this
	/*QJsonObject s;
	QJsonObject x;
	x.insert("t","S");
	x.insert("d","This string will be echoed by process()");
	s.insert("Echo_string", x);
	x.insert("t", "B");
	x.insert("d","Should the module fail in init()?");
	x.insert("default",false);
	s.insert("Fail_on_init", x);
	return s;*/
}

rapidjson::Value ExampleModule::publishActions(rapidjson::MemoryPoolAllocator<> &a) const {
	alert("ExampleModule::publishActions()");
	// TODO
	return Value(rapidjson::kNullType);
}

void ExampleModule::cleanup() {
	alert("ExampleModule::cleanup()");
}

void ExampleModule::handleReconfigure() {
	alert("ExampleModule::handleReconfigure()");
	alert(QString("There are %1 input/output columns").arg(outputColumns.size()));
}
