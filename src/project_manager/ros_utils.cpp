/**
 * @author Levi Armstrong
 * @date January 1, 2016
 *
 * @copyright Copyright (c) 2016, Southwest Research Institute
 *
 * @license Software License Agreement (Apache License)\n
 * \n
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at\n
 * \n
 * http://www.apache.org/licenses/LICENSE-2.0\n
 * \n
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "ros_utils.h"
#include "ros_project_constants.h"
#include "ros_packagexml_parser.h"
#include "ros_settings_page.h"
#include "ros_project_plugin.h"

#include <utils/fileutils.h>
#include <coreplugin/messagemanager.h>
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDirIterator>
#include <QStandardPaths>

namespace ROSProjectManager {
namespace Internal {

ROSUtils::ROSUtils()
{

}

QString ROSUtils::buildTypeName(const ROSUtils::BuildType &buildType)
{
    switch (buildType) {
    case ROSUtils::BuildTypeDebug:
        return QLatin1String("Debug");
    case ROSUtils::BuildTypeMinSizeRel:
        return QLatin1String("Minimum Size Release");
    case ROSUtils::BuildTypeRelWithDebInfo:
        return QLatin1String("Release with Debug Information");
    case ROSUtils::BuildTypeRelease:
        return QLatin1String("Release");
    default:
        return QLatin1String("User Defined");
    }
}

bool ROSUtils::sourceROS(QProcess *process, const Utils::FileName &rosDistribution)
{
  bool results = sourceWorkspaceHelper(process, Utils::FileName(rosDistribution).appendPath(QLatin1String("setup.bash")).toString());
  if (!results)
    Core::MessageManager::write(QObject::tr("[ROS Warning] Faild to source ROS Distribution: %1.").arg(rosDistribution.toString()));

  return results;
}

bool ROSUtils::sourceWorkspace(QProcess *process, const WorkspaceInfo &workspaceInfo)
{
    if (!initializeWorkspace(process, workspaceInfo))
        return false;

    Utils::FileName bash(workspaceInfo.develPath);
    if (workspaceInfo.install)
      bash = Utils::FileName(workspaceInfo.installPath);

    bash.appendPath(QLatin1String("setup.bash"));
    if (bash.exists())
    {
        if (sourceWorkspaceHelper(process, bash.toString()))
            return true;
    }
    else
    {
        Core::MessageManager::write(QObject::tr("[ROS Warning] Failed to source workspace because this file does not exist: %1.").arg(bash.toString()));
        return true;
    }

    Core::MessageManager::write(QObject::tr("[ROS Warning] Failed to source workspace: %1.").arg(workspaceInfo.path.toString()));
    return false;
}

bool ROSUtils::isWorkspaceInitialized(const WorkspaceInfo &workspaceInfo)
{
    switch (workspaceInfo.buildSystem) {
    case ROSUtils::CatkinMake:
    {
        Utils::FileName topCMake(workspaceInfo.sourcePath);
        topCMake.appendPath(QLatin1String("CMakeLists.txt"));
        Utils::FileName catkin_workspace(workspaceInfo.path);
        catkin_workspace.appendPath(QLatin1String(".catkin_workspace"));

        if (topCMake.exists() && catkin_workspace.exists() && workspaceInfo.sourcePath.exists())
          return true;

        return false;
    }
    case ROSUtils::CatkinTools:
    {
        Utils::FileName catkin_tools(workspaceInfo.path);
        catkin_tools.appendPath(QLatin1String(".catkin_tools"));
        if (catkin_tools.exists() && workspaceInfo.sourcePath.exists())
          return true;

        return false;
    }
    case ROSUtils::Colcon:
    {
        if (workspaceInfo.sourcePath.exists())
          return true;

        return false;
    }
    }

    return false;
}

bool ROSUtils::initializeWorkspaceFolders(const WorkspaceInfo &workspaceInfo)
{
    if (!workspaceInfo.sourcePath.exists())
        if( ! QDir().mkpath(workspaceInfo.sourcePath.toString()) ) {
            Core::MessageManager::write(QObject::tr("[ROS Warning] Failed to initialize workspace folder: %1.").arg(workspaceInfo.sourcePath.toString()));
            return false;
        }

    if (!workspaceInfo.logPath.exists())
        if( ! QDir().mkpath(workspaceInfo.logPath.toString()) ) {
            Core::MessageManager::write(QObject::tr("[ROS Warning] Failed to initialize workspace folder: %1.").arg(workspaceInfo.logPath.toString()));
            return false;
        }

    if (!workspaceInfo.buildPath.exists())
        if( ! QDir().mkpath(workspaceInfo.buildPath.toString()) ) {
            Core::MessageManager::write(QObject::tr("[ROS Warning] Failed to initialize workspace folder: %1.").arg(workspaceInfo.buildPath.toString()));
            return false;
        }

    if (!workspaceInfo.develPath.exists())
        if( ! QDir().mkpath(workspaceInfo.develPath.toString()) ) {
            Core::MessageManager::write(QObject::tr("[ROS Warning] Failed to initialize workspace folder: %1.").arg(workspaceInfo.develPath.toString()));
            return false;
        }

    if (!workspaceInfo.installPath.exists())
        if( ! QDir().mkpath(workspaceInfo.installPath.toString()) ) {
            Core::MessageManager::write(QObject::tr("[ROS Warning] Failed to initialize workspace folder: %1.").arg(workspaceInfo.installPath.toString()));
            return false;
        }

    return true;
}

bool ROSUtils::initializeWorkspace(QProcess *process, const WorkspaceInfo &workspaceInfo)
{
    WorkspaceInfo workspace = workspaceInfo;

    if (sourceROS(process, workspaceInfo.rosDistribution))
        if (!isWorkspaceInitialized(workspaceInfo))
        {
            switch (workspaceInfo.buildSystem) {
            case CatkinMake:
            {
                if( !initializeWorkspaceFolders(workspaceInfo) )
                    return false;

                process->setWorkingDirectory(workspaceInfo.sourcePath.toString());
                process->start(QLatin1String("bash"), QStringList() << QStringList() << QLatin1String("-c") << QLatin1String("catkin_init_workspace"));

                if( !process->waitForFinished() )
                    return false;

                break;
            }
            case CatkinTools:
            {
                setCatkinToolsDefaultProfile(workspace.path);

                workspace = ROSUtils::getWorkspaceInfo(workspace.path,
                                                       workspace.buildSystem,
                                                       workspace.rosDistribution);

                if( !initializeWorkspaceFolders(workspace) )
                    return false;

                process->setWorkingDirectory(workspace.path.toString());
                process->start(QLatin1String("bash"), QStringList() << QLatin1String("-c") << QLatin1String("catkin init"));

                if( !process->waitForFinished() )
                    return false;

                break;
            }
            case Colcon:
            {
                workspace = ROSUtils::getWorkspaceInfo(workspace.path,
                                                       workspace.buildSystem,
                                                       workspace.rosDistribution);

                if( !initializeWorkspaceFolders(workspace) )
                    return false;

                break;
            }
         }

        if (process->exitStatus() != QProcess::CrashExit)
            return buildWorkspace(process, workspace);

        Core::MessageManager::write(QObject::tr("[ROS Warning] Failed to initialize workspace: %1.").arg(workspace.path.toString()));
        return false;
    }

    return true;
}

bool ROSUtils::buildWorkspace(QProcess *process, const WorkspaceInfo &workspaceInfo)
{
    switch(workspaceInfo.buildSystem) {
    case CatkinMake:
    {
        process->setWorkingDirectory(workspaceInfo.path.toString());
        process->start(QLatin1String("bash"), QStringList() << QLatin1String("-c") << QLatin1String("catkin_make --cmake-args -G \"CodeBlocks - Unix Makefiles\""));
        process->waitForFinished();
        break;
    }
    case CatkinTools:
    {
        process->setWorkingDirectory(workspaceInfo.path.toString());
        process->start(QLatin1String("bash"), QStringList() << QLatin1String("-c") << QLatin1String("catkin build --cmake-args -G \"CodeBlocks - Unix Makefiles\""));
        process->waitForFinished();
        break;
    }
    case Colcon:
    {
        process->setWorkingDirectory(workspaceInfo.path.toString());
        process->start(QLatin1String("bash"), QStringList() << QLatin1String("-c") << QLatin1String("colcon build --cmake-args -G \"CodeBlocks - Unix Makefiles\""));
        process->waitForFinished();
        break;
    }
    }

    if (process->exitStatus() != QProcess::CrashExit)
        return true;

    Core::MessageManager::write(QObject::tr("[ROS Warning] Failed to build workspace: %1.").arg(workspaceInfo.path.toString()));
    return false;
}

QList<Utils::FileName> ROSUtils::installedDistributions()
{
  QSharedPointer<ROSSettings> ros_settings = ROSProjectPlugin::instance()->settings();
  Utils::FileName custom_ros_path = Utils::FileName::fromString(ros_settings->custom_dist_path);
  QList<Utils::FileName> distributions;
  if(custom_ros_path.exists())
  {
    QDir custom_dir(custom_ros_path.toString());

    custom_dir.setFilter(QDir::NoDotAndDotDot | QDir::Dirs);
    for (auto entry : custom_dir.entryList())
    {
      Utils::FileName path(custom_ros_path);
      path.appendPath(entry);

      Utils::FileName setup_file = path;
      setup_file.appendString(QString("/setup.bash"));

      if (setup_file.exists())
      {
        distributions.append(path);
      }
    }
  }

  Utils::FileName default_ros_path = Utils::FileName::fromString(ros_settings->default_dist_path);
  if (default_ros_path.exists())
  {
    QDir ros_opt(default_ros_path.toString());

    ros_opt.setFilter(QDir::NoDotAndDotDot | QDir::Dirs);
    for (auto entry : ros_opt.entryList())
    {
      Utils::FileName path = Utils::FileName::fromString(QLatin1String(ROSProjectManager::Constants::ROS_INSTALL_DIRECTORY));
      path.appendPath(entry);

      Utils::FileName setup_file = path;
      setup_file.appendString(QString("/setup.bash"));

      if (setup_file.exists())
      {
        distributions.append(path);
      }
    }
  }

  if (distributions.isEmpty())
      Core::MessageManager::write(QObject::tr("[ROS Error] ROS Does not appear to be installed.\n Check ROS Settings page to verify that the install location is valid."));

  return distributions;
}

bool ROSUtils::sourceWorkspaceHelper(QProcess *process, const QString &path)
{
  QStringList env_list;
  QString cmd = QLatin1String("source ") + path + QLatin1String(" && env");

  process->start(QLatin1String("bash"));
  process->waitForStarted();
  process->write(cmd.toLatin1());
  process->closeWriteChannel();
  process->waitForFinished();

  if (process->exitStatus() != QProcess::CrashExit)
  {
    QString output = QString::fromStdString(process->readAllStandardOutput().toStdString());
    env_list = output.split(QRegExp("[\r\n]"), QString::SkipEmptyParts);

    Utils::Environment env(env_list);
    process->setProcessEnvironment(env.toProcessEnvironment());
    return true;
  }
  return false;
}

bool ROSUtils::gererateQtCreatorWorkspaceFile(QXmlStreamWriter &xmlFile, const ROSProjectFileContent &content)
{
    xmlFile.setAutoFormatting(true);
    xmlFile.writeStartDocument();
    xmlFile.writeStartElement(QLatin1String("Workspace"));

    xmlFile.writeStartElement(QLatin1String("Distribution"));
    xmlFile.writeAttribute(QLatin1String("path"), content.distribution.toString());
    xmlFile.writeEndElement();

    xmlFile.writeStartElement(QLatin1String("DefaultBuildSystem"));
    xmlFile.writeAttribute(QLatin1String("value"), QString::number(content.defaultBuildSystem));
    xmlFile.writeEndElement();

    xmlFile.writeStartElement(QLatin1String("WatchDirectories"));
    for (const QString& str : content.watchDirectories)
        xmlFile.writeTextElement(QLatin1String("Directory"), str);

    xmlFile.writeEndElement();

    xmlFile.writeEndElement();
    xmlFile.writeEndDocument();
    return xmlFile.hasError();
}

bool ROSUtils::parseQtCreatorWorkspaceFile(const Utils::FileName &filePath, ROSProjectFileContent &content)
{
    QXmlStreamReader workspaceXml;
    QFile workspaceFile(filePath.toString());
    if (workspaceFile.open(QFile::ReadOnly | QFile::Text))
    {
        content.watchDirectories.clear();

        workspaceXml.setDevice(&workspaceFile);
        while(workspaceXml.readNextStartElement())
        {
            if (workspaceXml.name() == QLatin1String("Distribution"))
            {
                QList<Utils::FileName> distributions = ROSUtils::installedDistributions();
                QXmlStreamAttributes attributes = workspaceXml.attributes();
                if (attributes.hasAttribute(QLatin1String("path")))
                {
                    content.distribution = Utils::FileName::fromString(attributes.value(QLatin1String("path")).toString());
                    if (!distributions.empty() && !distributions.contains(content.distribution))
                    {
                        Core::MessageManager::write(QObject::tr("[ROS Error] Project file distribution [%1] is not installed. Setting to [%2], if incorrect modify project file [%3].").arg(content.distribution.toString(), distributions.first().toString(), filePath.fileName()));
                        content.distribution = distributions.first();
                    }
                }
                else
                {
                    if (!distributions.isEmpty())
                    {
                        content.distribution = distributions.first();
                        Core::MessageManager::write(QObject::tr("[ROS Error] Unable to find ROS distributions."));
                    }
                    else
                    {
                        Core::MessageManager::write(QObject::tr("[ROS Error] Project file Distribution tag did not have a name attribute."));
                    }
                }

                workspaceXml.readNextStartElement();
            }
            else if (workspaceXml.name() == QLatin1String("DefaultBuildSystem"))
            {
                QXmlStreamAttributes attributes = workspaceXml.attributes();
                if (attributes.hasAttribute(QLatin1String("value")))
                {
                    content.defaultBuildSystem = (ROSUtils::BuildSystem)attributes.value(QLatin1String("value")).toInt();
                }
                else
                {
                    content.defaultBuildSystem = ROSUtils::CatkinMake;
                    Core::MessageManager::write(QObject::tr("[ROS Warning] Project file DefaultBuildSystem tag did not have a value attribute."));
                }

                workspaceXml.readNextStartElement();
            }
            else if (workspaceXml.name() == QLatin1String("WatchDirectories"))
            {
                while(workspaceXml.readNextStartElement())
                    if(workspaceXml.name() == QLatin1String("Directory"))
                        content.watchDirectories.append(workspaceXml.readElementText());
            }
        }

        // If there is not a directory tag it will watch the workspace directory.
        if (content.watchDirectories.size() == 0)
            content.watchDirectories.append("");

        return true;
    }

    Core::MessageManager::write(QObject::tr("[ROS Error] Error opening Workspace Project File: %1.").arg(filePath.toString()));
    return false;
}

void ROSUtils::getDefaultFolderContentFilters(QStringList& folderNameFilters, QStringList& fileNameFilters)
{
  folderNameFilters.push_back("\\.git");
  fileNameFilters.push_back("^.*\\.autosave");
}

ROSUtils::FolderContent ROSUtils::getFolderContent(const QString &folder, const QStringList& folderNameFilters, const QStringList& fileNameFilters)
{
  ROSUtils::FolderContent content;

  // Get Directory data
  content.directories = QDir(folder).entryList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Hidden);
  content.removeDirectories(folderNameFilters);

  content.files = QDir(folder).entryList(QDir::NoDotAndDotDot | QDir::Files | QDir::Hidden);
  content.removeFiles(fileNameFilters);

  return content;
}

QHash<QString, ROSUtils::FolderContent> ROSUtils::getFolderContentRecurisve(const Utils::FileName &folderPath, QStringList &fileList, QStringList& directoryList)
{
    QHash<QString, ROSUtils::FolderContent> workspaceFiles;

    QString folder = folderPath.toString();

    // Need to remove unwanted directories
    QStringList folderNameFilters, fileNameFilters;
    getDefaultFolderContentFilters(folderNameFilters, fileNameFilters);

    ROSUtils::FolderContent content = getFolderContent(folder, folderNameFilters, fileNameFilters);

    workspaceFiles[folder] = content;

    for (const QString& file : content.files)
        fileList.append(QDir(folder).absoluteFilePath(file));

    for (const QString& directory : content.directories)
        directoryList.append(QDir(folder).absoluteFilePath(directory));

    // Get SubDirectory Information
    const QDir subDir(folder);
    QDirIterator itSrc(subDir.absolutePath(), QDir::NoDotAndDotDot | QDir::Dirs | QDir::Hidden, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    QList<QString> excludeDir;
    while (itSrc.hasNext())
    {
        folder = itSrc.next();

        QString folder_name = Utils::FileName::fromString(folder).fileName();
        bool found = false;
        for (auto filter : folderNameFilters)
        {
          QRegularExpression rx(filter);
          if (rx.match(folder_name).hasMatch())
          {
            excludeDir.push_back(folder + QLatin1Char('/'));
            found = true;
            break;
          }
        }

        if (found)
          continue;

        bool skip = false;
        for (const QString& exclude : excludeDir)
        {
            if (folder.startsWith(exclude))
            {
                skip = true;
                break;
            }
        }

        if (skip) continue;

        content = getFolderContent(folder, folderNameFilters, fileNameFilters);

        workspaceFiles[folder] = content;

        for (const QString& file : content.files)
            fileList.append(QDir(folder).absoluteFilePath(file));

        for (const QString& directory : content.directories)
            directoryList.append(QDir(folder).absoluteFilePath(directory));
    }

    return workspaceFiles;
}

ROSUtils::PackageInfoMap ROSUtils::getWorkspacePackageInfo(const WorkspaceInfo &workspaceInfo, const PackageInfoMap *cachedPackageInfo)
{
    PackageInfoMap wsPackageInfo;
    QMap<QString, QString> packages =  ROSUtils::getWorkspacePackagePaths(workspaceInfo);

    QMapIterator<QString, QString> it(packages);
    while (it.hasNext())
    {
        it.next();
        Utils::FileName pkgXml = Utils::FileName::fromString(it.value()).appendPath("package.xml");
        ROSUtils::PackageInfo packageInfo;
        ROSPackageXmlParser pkgParser;
        if (pkgParser.parsePackageXml(pkgXml, packageInfo))
        {
            if (packageInfo.metapackage)
                continue;

            wsPackageInfo.insert(packageInfo.name, packageInfo);
            continue;
        }

        // Check if there is cached build info available
        if (cachedPackageInfo)
        {
            auto packIt = cachedPackageInfo->find(packageInfo.name);
            if (packIt != cachedPackageInfo->end())
            {
                Core::MessageManager::write(QObject::tr("[ROS Info] Using cached package information for package: %1.").arg(packageInfo.name));
                wsPackageInfo.insert(packIt.value().name, packIt.value());
            }
        }
    }
    return wsPackageInfo;
}

ROSUtils::PackageBuildInfoMap ROSUtils::getWorkspacePackageBuildInfo(const WorkspaceInfo &workspaceInfo,
                                                                     const PackageInfoMap &packageInfo,
                                                                     const PackageBuildInfoMap *cachedPackageBuildInfo)
{
    PackageBuildInfoMap wsBuildInfo;
    for (const PackageInfo& package : packageInfo)
    {
        PackageBuildInfo buildInfo(package);
        if (findPackageBuildDirectory(workspaceInfo, package, buildInfo.path))
        {
            // Get package's code block file
            buildInfo.cbpFile = buildInfo.path;
            buildInfo.cbpFile.appendPath(QString("%1.cbp").arg(package.name));

            if (buildInfo.cbpFile.exists())
            {
                if (ROSUtils::parseCodeBlocksFile(workspaceInfo, buildInfo))
                {
                    wsBuildInfo.insert(package.name, buildInfo);
                    continue;
                }
                else
                {
                    Core::MessageManager::write(QObject::tr("[ROS Warning] Unable to parse build information for package: %1.").arg(package.name));
                }
            }
            else
            {
                Core::MessageManager::write(QObject::tr("[ROS Warning] Unable to locate package %1 build file: %2.").arg(package.name, buildInfo.cbpFile.toString()));
            }
        }
        else
        {
            Core::MessageManager::write(QObject::tr("[ROS Warning] Unable to locate build directory for package: %1.").arg(package.name));
        }

        // Check if there is cached build info available
        if (cachedPackageBuildInfo)
        {
            auto packIt = cachedPackageBuildInfo->find(package.name);
            if (packIt != cachedPackageBuildInfo->end())
            {
                Core::MessageManager::write(QObject::tr("[ROS Info] Using cached package build information for package: %1.").arg(package.name));
                wsBuildInfo.insert(package.name, packIt.value());
            }
        }

    }

    return wsBuildInfo;
}

bool ROSUtils::parseCodeBlocksFile(const WorkspaceInfo &workspaceInfo, ROSUtils::PackageBuildInfo &buildInfo)
{

  // Parse CodeBlocks Project File
  // Need to search for all of the tags <Add directory="include path" />
  QXmlStreamReader cbpXml;

  QFile cbpFile(buildInfo.cbpFile.toString());
  if (!cbpFile.open(QFile::ReadOnly | QFile::Text))
  {
    Core::MessageManager::write(QObject::tr("[ROS Error] Error opening CodeBlocks Project File: %1.").arg(buildInfo.cbpFile.toString()));
    return false;
  }

  // make sure targets are cleared
  buildInfo.targets.clear();

  // build time include directory
  Utils::FileName buildtimeInclude(workspaceInfo.develPath);
  if (workspaceInfo.install)
    buildtimeInclude = Utils::FileName(workspaceInfo.installPath);

  buildtimeInclude = buildtimeInclude.appendPath(QLatin1String("include"));

  cbpXml.setDevice(&cbpFile);
  cbpXml.readNext();
  while(!cbpXml.atEnd())
  {
    if(cbpXml.isStartElement())
    {
      if(cbpXml.name() == QLatin1String("Target"))
      {
        QString targetName;
        QStringList targetLocalIncludes;
        QStringList targetSystemIncludes;
        TargetType targetType = UtilityType;
        if (cbpXml.attributes().hasAttribute("title"))
        {
          QString title =cbpXml.attributes().value("title").toString();
          if (!title.endsWith(QLatin1String("/fast")) && !title.endsWith(QLatin1String("_automoc")) && !title.startsWith(QLatin1String("gtest")))
          {
            targetName = title;
          }
          else
          {
            cbpXml.readNext();
            continue;
          }
        }
        else
        {
          cbpXml.readNext();
          continue;
        }

        cbpXml.readNext();
        while (cbpXml.name() != QLatin1String("Target"))
        {
            if(cbpXml.isStartElement())
            {
                if(cbpXml.name() == QLatin1String("Option"))
                {
                    if (cbpXml.attributes().hasAttribute("type"))
                    {
                        QString attribute_value = cbpXml.attributes().value("type").toString();
                        if (attribute_value == "0" || attribute_value == "1")
                            targetType = ExecutableType;
                        else if (attribute_value == "2")
                            targetType = StaticLibraryType;
                        else if (attribute_value == "3")
                            targetType = DynamicLibraryType;
                        else
                            targetType = UtilityType;
                    }
                }

                if(cbpXml.name() == QLatin1String("Add"))
                {
                    if (cbpXml.attributes().hasAttribute("directory"))
                    {
                        QString attribute_value = cbpXml.attributes().value("directory").toString();
                        if (attribute_value.startsWith(workspaceInfo.path.toString()))
                        {
                            if (!targetLocalIncludes.contains(attribute_value))
                              targetLocalIncludes.append(attribute_value);
                        }
                        else if(!targetSystemIncludes.contains(attribute_value))
                        {
                            targetSystemIncludes.append(attribute_value);
                        }
                    }
                }
            }
            cbpXml.readNext();
        }

        // Only need to add target types ExecutableType and StaticLibraryType to the code model
        if (targetType != UtilityType)
        {
            targetLocalIncludes.append(buildtimeInclude.toString());

            PackageTargetInfo targetInfo;
            targetInfo.name = targetName;
            targetInfo.type = targetType;
            targetInfo.flagsFile = Utils::FileName(buildInfo.path).appendPath("CMakeFiles").appendPath(QString("%1.dir").arg(targetName)).appendPath("flags.make");

            // The order matters so it will order local first then system
            targetInfo.includes = targetLocalIncludes;
            targetInfo.includes.append(targetSystemIncludes);

            buildInfo.targets.append(targetInfo);
        }
      }
    }
    cbpXml.readNext();
  }

//  Next search the package directory for any missed include folders
//  QString includePath;
//  QDirIterator itPackage(package.path, QStringList() << QLatin1String("include"), QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
//  while (itPackage.hasNext())
//  {
//    includePath = itPackage.next();
//    if(!workspace_includes.contains(includePath))
//      workspace_includes.append(includePath);
//  }

  for(auto it = buildInfo.targets.begin(); it != buildInfo.targets.end(); ++it)
  {
      // Next need to parse flags.cmake for flags and defines
      if (it->flagsFile.exists())
      {
          QFile flagsFile(it->flagsFile.toString());
          if (!flagsFile.open(QFile::ReadOnly | QFile::Text))
          {
            Core::MessageManager::write(QObject::tr("[ROS Error] Error opening flags file: %1.").arg(it->flagsFile.toString()));
            it->flags.append(QLatin1String("-std=c++11"));
            continue;
          }

          QTextStream flagsStream(&flagsFile);
          while (!flagsStream.atEnd()) {
              QString line = flagsStream.readLine().trimmed();
              if (line.startsWith("CXX_FLAGS ="))
                  it->flags = line.mid(11).trimmed().split(' ', QString::SkipEmptyParts);

              if (line.startsWith("CXX_DEFINES ="))
                  it->defines = line.mid(13).trimmed().split(' ', QString::SkipEmptyParts);

          }
      }
      else
      {
          Core::MessageManager::write(QObject::tr("[ROS Warning] Flags file does not exist: %1.").arg(it->flagsFile.toString()));
          it->flags.append(QLatin1String("-std=c++11"));
      }
  }

  return true;
}

QMap<QString, QString> ROSUtils::getROSPackages(const QStringList &env)
{
  QProcess process;
  QMap<QString, QString> package_map;
  QStringList tmp;

  process.setEnvironment(env);
  process.start(QLatin1String("bash"));
  process.waitForStarted();
  QString cmd = QLatin1String("rospack list"); // TODO: for ROS2 do 'ros2 pkg list'
  process.write(cmd.toLatin1());
  process.closeWriteChannel();
  process.waitForFinished();

  if (process.exitStatus() != QProcess::CrashExit)
  {
    QString output = QString::fromStdString(process.readAllStandardOutput().toStdString());
    QStringList package_list = output.split(QRegExp("[\r\n]"), QString::SkipEmptyParts);

    for (const QString& str : package_list)
    {
        tmp = str.split(QLatin1String(" "));
        package_map.insert(tmp[0],tmp[1]);
    }

    return package_map;
  }
  return QMap<QString, QString>();
}

QMap<QString, QString> ROSUtils::getWorkspacePackagePaths(const WorkspaceInfo &workspaceInfo)
{
    QMap<QString, QString> packageMap;

    const QDir srcDir(workspaceInfo.sourcePath.toString());
    if(srcDir.exists())
    {
      QDirIterator it(srcDir.absolutePath(),QStringList() << QLatin1String("package.xml"), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
      while (it.hasNext())
      {
        QFileInfo packageFile(it.next());
        packageMap.insert(packageFile.absoluteDir().dirName(), packageFile.absoluteDir().absolutePath());
      }
    }
    else
    {
        Core::MessageManager::write(QObject::tr("[ROS Error] Workspace source directory does not exist: %1.").arg(workspaceInfo.sourcePath.toString()));
    }

    return packageMap;
}

QMap<QString, QString> ROSUtils::getROSPackageLaunchFiles(const QString &packagePath)
{
  QMap<QString, QString> launchFiles;
  if(!packagePath.isEmpty())
  {
    const QDir srcDir(packagePath);
    QDirIterator it(srcDir.absolutePath(),QStringList() << QLatin1String("*.launch"), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);

    while (it.hasNext())
    {
      QFileInfo launchFile(it.next());
      launchFiles.insert(launchFile.fileName(), launchFile.absoluteFilePath());
    }
  }

  return launchFiles;
}

QMap<QString, QString> ROSUtils::getROSPackageExecutables(const QString &packageName, const QStringList &env)
{

  QProcess process;
  QMap<QString, QString> package_executables;

  process.setEnvironment(env);
  process.start(QLatin1String("bash"));
  process.waitForStarted();
  QString cmd = QLatin1String("catkin_find --without-underlays --libexec ") + packageName;
  process.write(cmd.toLatin1());
  process.closeWriteChannel();
  process.waitForFinished();

  if (process.exitStatus() != QProcess::CrashExit)
  {
    QString output = QString::fromStdString(process.readAllStandardOutput().toStdString());
    QStringList loc_list = output.split(QRegExp("[\r\n]"), QString::SkipEmptyParts);

    if (loc_list.size() > 0)
    {
      const QDir srcDir(loc_list[0]);
      QDirIterator it(srcDir.absolutePath(), QDir::Files | QDir::Executable | QDir::NoDotAndDotDot, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);

      while (it.hasNext())
      {
        QFileInfo executableFile(it.next());
        package_executables.insert(executableFile.fileName(), executableFile.absoluteFilePath());
      }

      return package_executables;
    }
  }

  return QMap<QString, QString>();
}

Utils::FileName ROSUtils::getCatkinToolsProfilesPath(const Utils::FileName &workspaceDir)
{
    Utils::FileName profiles(workspaceDir);
    profiles.appendPath(QLatin1String(".catkin_tools"));
    profiles.appendPath(QLatin1String("profiles"));
    return profiles;
}

Utils::FileName ROSUtils::getCatkinToolsProfilesYamlFile(const Utils::FileName &workspaceDir)
{
    Utils::FileName profiles = getCatkinToolsProfilesPath(workspaceDir);
    profiles.appendPath(QLatin1String("profiles.yaml"));
    return profiles;
}

bool ROSUtils::setCatkinToolsProfilesYamlFile(const Utils::FileName &workspaceDir,
                                              const QString &profileName)
{
    YAML::Node config;

    Utils::FileName profiles = getCatkinToolsProfilesYamlFile(workspaceDir);

    if (profiles.exists())
        config = YAML::LoadFile(profiles.toString().toStdString());

    config["active"] = profileName.toStdString();

    std::ofstream fout(profiles.toString().toStdString());

    if( ! fout.is_open())
        return false;

    fout << config; // dump it back into the file

    return true;
}

Utils::FileName ROSUtils::getCatkinToolsProfilePath(const Utils::FileName &workspaceDir, const QString &profileName)
{
    Utils::FileName profile = getCatkinToolsProfilesPath(workspaceDir);
    profile.appendPath(profileName);
    return profile;
}

Utils::FileName ROSUtils::getCatkinToolsProfileConfigFile(const Utils::FileName &workspaceDir, const QString &profileName)
{
    Utils::FileName profile = getCatkinToolsProfilePath(workspaceDir, profileName);
    profile.appendPath("config.yaml");
    return profile;
}

bool ROSUtils::isCatkinToolsProfileConfigValid(const Utils::FileName& configPath)
{
  if (!configPath.exists())
    return false;

  YAML::Node config = YAML::LoadFile(configPath.toString().toStdString());
  if (config.IsNull())
    return false;

  if (!config["source_space"].IsDefined())
    return false;

  if (!config["build_space"].IsDefined())
    return false;

  if (!config["devel_space"].IsDefined())
    return false;

  if (!config["install_space"].IsDefined())
    return false;

  if (!config["log_space"].IsDefined())
    return false;

  if (!config["install"].IsDefined())
    return false;

  return true;
}

bool ROSUtils::removeCatkinToolsProfile(const Utils::FileName &workspaceDir, const QString &profileName)
{
    QString activeProfile = getCatkinToolsActiveProfile(workspaceDir);

    if( activeProfile.length() )
    {
        Utils::FileName profiles = getCatkinToolsProfilePath(workspaceDir, profileName);
        QDir d(profiles.toString());
        if (d.exists())
        {
            if (!d.removeRecursively())
                return false;

            if (activeProfile == profileName)
                setCatkinToolsActiveProfile(workspaceDir, QLatin1Literal("default"));
        }
    }

    return true;
}

bool ROSUtils::renameCatkinToolsProfile(const Utils::FileName &workspaceDir, const QString &oldProfileName, const QString &newProfileName)
{
    Utils::FileName profile = getCatkinToolsProfilePath(workspaceDir, oldProfileName);
    QDir d(profile.toString());
    if (d.exists())
        return d.rename(oldProfileName, newProfileName);

    return createCatkinToolsProfile(workspaceDir, newProfileName, true);
}

bool ROSUtils::createCatkinToolsProfile(const Utils::FileName &workspaceDir, const QString profileName, bool overwrite)
{
    Utils::FileName config = getCatkinToolsProfileConfigFile(workspaceDir, profileName);

    QDir().mkpath(getCatkinToolsProfilePath(workspaceDir, profileName).toString());
    if (overwrite)
      QFile::remove(config.toString());

    return (QFile::copy(":rosproject/config.yaml", config.toString()) &&
            QFile::setPermissions(config.toString(),
                                  QFile::ReadUser |
                                  QFile::WriteUser |
                                  QFile::ReadGroup |
                                  QFile::WriteGroup));
}

bool ROSUtils::cloneCatkinToolsProfile(const Utils::FileName &workspaceDir, const QString &profileName, const QString &newProfileName)
{
    Utils::FileName copyConfig = getCatkinToolsProfileConfigFile(workspaceDir, profileName);
    Utils::FileName newConfig = getCatkinToolsProfileConfigFile(workspaceDir, newProfileName);

    if (!isCatkinToolsProfileConfigValid(copyConfig))
        return createCatkinToolsProfile(workspaceDir, profileName, true);

    QDir().mkpath(getCatkinToolsProfilePath(workspaceDir, newProfileName).toString());
    return QFile::copy(copyConfig.toString(), newConfig.toString());
}

QString ROSUtils::getCatkinToolsActiveProfile(const Utils::FileName &workspaceDir)
{
    QString activeProfile;
    Utils::FileName profiles = getCatkinToolsProfilesYamlFile(workspaceDir);
    if (profiles.exists())
    {
        YAML::Node config = YAML::LoadFile(profiles.toString().toStdString());
        activeProfile = QString::fromStdString(config["active"].as<std::string>());
    }
    else
        return QString("");

    return activeProfile;
}

QString ROSUtils::setCatkinToolsDefaultProfile(const Utils::FileName &workspaceDir)
{
    QString defaultProfile("default");

    // This will create the profile if it does not exist with default config.
    setCatkinToolsActiveProfile(workspaceDir, defaultProfile);

    return defaultProfile;
}

bool ROSUtils::setCatkinToolsActiveProfile(const Utils::FileName &workspaceDir, const QString &profileName)
{
    // Create profiles directory if it does not exist
    QDir().mkpath(getCatkinToolsProfilesPath(workspaceDir).toString());

    if( ! setCatkinToolsProfilesYamlFile(workspaceDir, profileName) )
        return false;

    // This will create the profile if it does not exist with default config.
    createCatkinToolsProfile(workspaceDir, profileName, false);

    return true;
}

QStringList ROSUtils::getCatkinToolsProfileNames(const Utils::FileName &workspaceDir)
{
    Utils::FileName profiles = getCatkinToolsProfilesPath(workspaceDir);
    if (profiles.exists())
    {
        QDir d(profiles.toString());
        QStringList profileNames = d.entryList(QDir::AllDirs | QDir::NoDotAndDotDot);
        if (!profileNames.empty())
            return profileNames;
    }

    // If there are currently no profiles, create a default profile.
    createCatkinToolsProfile(workspaceDir, QLatin1String("default"), true);
    return QStringList() << QLatin1String("default");
}

Utils::FileName ROSUtils::getCatkinToolsProfile(const Utils::FileName &workspaceDir, const QString &profileName)
{
    Utils::FileName profile = ROSUtils::getCatkinToolsProfileConfigFile(workspaceDir, profileName);
    if(!isCatkinToolsProfileConfigValid(profile))
        createCatkinToolsProfile(workspaceDir, profileName, true);

    return profile;
}

QString ROSUtils::getCMakeBuildTypeArgument(ROSUtils::BuildType &buildType)
{
    switch (buildType) {
    case ROSUtils::BuildTypeDebug:
        return QLatin1String("-DCMAKE_BUILD_TYPE=Debug");
    case ROSUtils::BuildTypeMinSizeRel:
        return QLatin1String("-DCMAKE_BUILD_TYPE=MinSizeRel");
    case ROSUtils::BuildTypeRelWithDebInfo:
        return QLatin1String("-DCMAKE_BUILD_TYPE=RelWithDebInfo");
    case ROSUtils::BuildTypeRelease:
        return QLatin1String("-DCMAKE_BUILD_TYPE=Release");
    default:
        return QLatin1String("");
    }
}

ROSUtils::WorkspaceInfo ROSUtils::getWorkspaceInfo(const Utils::FileName &workspaceDir,
                                                   const BuildSystem &buildSystem,
                                                   const Utils::FileName &rosDistribution)
{
    WorkspaceInfo space;
    space.path = workspaceDir;
    space.buildSystem = buildSystem;
    space.rosDistribution = rosDistribution;

    switch(buildSystem) {
    case CatkinMake:
    {
        space.sourcePath = Utils::FileName(workspaceDir).appendPath("src");
        space.buildPath = Utils::FileName(workspaceDir).appendPath("build");
        space.develPath = Utils::FileName(workspaceDir).appendPath("devel");
        space.installPath = Utils::FileName(workspaceDir).appendPath("install");
        space.logPath = Utils::FileName(workspaceDir).appendPath("logs");
        space.install = false; //TODO: Need to find how best to determine if installing
        break;
    }
    case CatkinTools:
    {
        YAML::Node config;
        QString activeProfile = getCatkinToolsActiveProfile(workspaceDir);

        if( activeProfile.length() )
        {
            Utils::FileName configPath = getCatkinToolsProfileConfigFile(workspaceDir, activeProfile);
            if (!isCatkinToolsProfileConfigValid(configPath))
              createCatkinToolsProfile(workspaceDir, activeProfile, true);

            config = YAML::LoadFile(configPath.toString().toStdString());
            space.sourcePath = Utils::FileName(workspaceDir).appendPath(QString::fromStdString(config["source_space"].as<std::string>()));
            space.buildPath = Utils::FileName(workspaceDir).appendPath(QString::fromStdString(config["build_space"].as<std::string>()));
            space.develPath = Utils::FileName(workspaceDir).appendPath(QString::fromStdString(config["devel_space"].as<std::string>()));
            space.installPath = Utils::FileName(workspaceDir).appendPath(QString::fromStdString(config["install_space"].as<std::string>()));
            space.logPath = Utils::FileName(workspaceDir).appendPath(QString::fromStdString(config["log_space"].as<std::string>()));
            space.install = config["install"].as<bool>();
        }
        else
        {
            // TODO: This is temporary fix since at time of CatkinTools.workspace creation
            // <Directory>.</Directory> is created instead of <Directory>src</Directory>
            // which causes random crash in file watcher reading "." instead of "src"
            space.sourcePath = Utils::FileName(workspaceDir).appendPath(QString::fromStdString("src"));
        }
        break;
    }
    case Colcon:
    {
        space.sourcePath = Utils::FileName(workspaceDir).appendPath("src");
        space.buildPath = Utils::FileName(workspaceDir).appendPath("build");
        space.develPath = Utils::FileName(workspaceDir).appendPath("install"); // Colcon does not have devel space setting to install
        space.installPath = Utils::FileName(workspaceDir).appendPath("install");
        space.logPath = Utils::FileName(workspaceDir).appendPath("log");
        space.install = true; // Calcon always uses the install space.
        break;
    }
    }

    return space;
}

QProcessEnvironment ROSUtils::getWorkspaceEnvironment(const WorkspaceInfo &workspaceInfo, const Utils::Environment& current_environment)
{
    QProcess process;

    process.setProcessEnvironment(current_environment.toProcessEnvironment());

    sourceWorkspace(&process, workspaceInfo);

    QProcessEnvironment env = process.processEnvironment();
    env.insert("PWD", workspaceInfo.path.toString());
    env.insert("TERM", "xterm");

    return env;
}

bool ROSUtils::findPackageBuildDirectory(const WorkspaceInfo &workspaceInfo, const PackageInfo &packageInfo, Utils::FileName &packageBuildPath)
{
    packageBuildPath = workspaceInfo.buildPath;
    switch(workspaceInfo.buildSystem) {
    case CatkinMake:
    {
        QString diff = packageInfo.path.toString();
        diff.remove(workspaceInfo.sourcePath.toString());
        packageBuildPath.appendString(diff);
        break;
    }
    case CatkinTools:
    {
        packageBuildPath = workspaceInfo.buildPath;
        packageBuildPath.appendPath(packageInfo.name);
        break;
    }
    case Colcon:
    {
        packageBuildPath = workspaceInfo.buildPath;
        packageBuildPath.appendPath(packageInfo.name);
        break;
    }
    }

    if (!QDir(packageBuildPath.toString()).exists())
        return false;

    return true;
}

bool ROSUtils::PackageInfo::exists() const
{
    return QDir(path.toString()).exists();
}

bool ROSUtils::PackageBuildInfo::exists() const
{
    return QDir(path.toString()).exists();
}

} //namespace Internal
} //namespace ROSProjectManager
