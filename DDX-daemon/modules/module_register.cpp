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

#ifndef MODULE_REGISTER_CPP
#define MODULE_REGISTER_CPP
#include "../unitmanager.h"

// Include Module headers here
#include "examplemodule.h"
#include "genmod.h"

void UnitManager::registerModules() {
	// List all Modules here (1 of 2)
	modules.insert("ExampleModule", ExampleModule::staticMetaObject);
	modules.insert("GenMod", GenMod::staticMetaObject);
}

QMap<QString, QString> UnitManager::getModuleDescriptions() const {
	QMap<QString, QString> m;
	
	// List all Modules here (2 of 2)
	m.insert("ExampleModule", tr("An example module"));
	m.insert("GenMod", tr("General modifications (TODO)"));
	
	return m;
}

#endif // MODULE_REGISTER_CPP