//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "iomap_sec.h"
#include "complexitem.h"
#include "map.h"
#include "tile.h"
#include "item.h"
#include "gui.h"
#include "materials.h"
#include "palette_window.h"
#include "creatures.h"
#include "creature.h"
#include "spawn.h"
#include "house.h"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace fs = std::filesystem;

bool IOMapSec::objectInfoLoaded = false;
std::map<uint16_t, IOMapSec::SecObjectInfo> IOMapSec::objectInfo;
std::map<int, IOMapSec::SecObjectType> IOMapSec::objectTypes;
bool IOMapSec::objectTypesLoaded = false;
std::string IOMapSec::objectsSrvPath;
IOMapSec::SecObjectConfig IOMapSec::objectConfig;
std::map<int, IOMapSec::SecHouseArea> IOMapSec::houseAreas;
bool IOMapSec::monsterTypesLoaded = false;
std::map<int, IOMapSec::SecMonsterType> IOMapSec::monsterTypes;
std::vector<IOMapSec::SecSpawnEntry> IOMapSec::spawnEntries;
IOMapSec::SecMapBounds IOMapSec::mapBounds;

// Case-insensitive string comparison
static bool iequals(const std::string& a, const std::string& b) {
  if(a.size() != b.size()) return false;
  for(size_t i = 0; i < a.size(); ++i) {
    if(std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
  }
  return true;
}

// =============================================================================
// Parser helpers
// =============================================================================

void IOMapSec::skipWhitespace(const std::string& line, size_t& pos) {
  while(pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
}

int32_t IOMapSec::readNumber(const std::string& line, size_t& pos) {
  skipWhitespace(line, pos);
  bool negative = false;
  if(pos < line.size() && line[pos] == '-') {
    negative = true;
    ++pos;
  }
  int64_t val = 0;
  while(pos < line.size() && std::isdigit((unsigned char)line[pos])) {
    val = val * 10 + (line[pos] - '0');
    ++pos;
  }
  return (int32_t)(negative ? -val : val);
}

std::string IOMapSec::readIdentifier(const std::string& line, size_t& pos) {
  skipWhitespace(line, pos);
  std::string result;
  while(pos < line.size() && (std::isalnum((unsigned char)line[pos]) || line[pos] == '_')) {
    result += line[pos];
    ++pos;
  }
  return result;
}

std::string IOMapSec::readQuotedString(const std::string& line, size_t& pos) {
  // Expect opening quote
  if(pos >= line.size() || line[pos] != '"') return "";
  ++pos;
  std::string result;
  while(pos < line.size() && line[pos] != '"') {
    if(line[pos] == '\\' && pos + 1 < line.size()) {
      ++pos;
      if(line[pos] == 'n') result += '\n';
      else if(line[pos] == '"') result += '"';
      else if(line[pos] == '\\') result += '\\';
      else { result += '\\'; result += line[pos]; }
    } else {
      result += line[pos];
    }
    ++pos;
  }
  if(pos < line.size()) ++pos; // skip closing quote
  return result;
}

bool IOMapSec::parseAttributes(const std::string& line, size_t& pos, ParsedItem& item) {
  while(pos < line.size()) {
    skipWhitespace(line, pos);
    if(pos >= line.size()) break;
    char c = line[pos];
    // If we hit comma or closing brace, we're done with this item's attributes
    if(c == ',' || c == '}') break;

    std::string attrName = readIdentifier(line, pos);
    if(attrName.empty()) break;

    skipWhitespace(line, pos);
    if(pos >= line.size() || line[pos] != '=') {
      warning("Expected '=' after attribute '%s'", attrName.c_str());
      return false;
    }
    ++pos; // skip '='

    if(iequals(attrName, "Content")) {
      skipWhitespace(line, pos);
      if(pos >= line.size() || line[pos] != '{') {
        warning("Expected '{' after Content=");
        return false;
      }
      ++pos; // skip '{'
      if(!parseItemList(line, pos, item.children)) return false;
      // parseItemList consumes the closing '}'
    } else if(iequals(attrName, "String")) {
      skipWhitespace(line, pos);
      item.text = readQuotedString(line, pos);
      item.hasText = true;
    } else if(iequals(attrName, "Editor")) {
      skipWhitespace(line, pos);
      item.editor = readQuotedString(line, pos);
      item.hasEditor = true;
    } else if(iequals(attrName, "Amount")) {
      item.amount = readNumber(line, pos);
    } else if(iequals(attrName, "Charges")) {
      item.charges = readNumber(line, pos);
    } else if(iequals(attrName, "KeyNumber")) {
      item.keyNumber = readNumber(line, pos);
    } else if(iequals(attrName, "KeyholeNumber")) {
      item.keyholeNumber = readNumber(line, pos);
    } else if(iequals(attrName, "Level")) {
      item.doorLevel = readNumber(line, pos);
    } else if(iequals(attrName, "DoorQuestNumber")) {
      item.doorQuestNumber = readNumber(line, pos);
    } else if(iequals(attrName, "DoorQuestValue")) {
      item.doorQuestValue = readNumber(line, pos);
    } else if(iequals(attrName, "ChestQuestNumber")) {
      item.chestQuestNumber = readNumber(line, pos);
    } else if(iequals(attrName, "ContainerLiquidType")) {
      item.containerLiquidType = readNumber(line, pos);
    } else if(iequals(attrName, "PoolLiquidType")) {
      item.poolLiquidType = readNumber(line, pos);
    } else if(iequals(attrName, "AbsTeleportDestination")) {
      item.absTeleportDest = readNumber(line, pos);
    } else if(iequals(attrName, "Responsible")) {
      item.responsible = readNumber(line, pos);
    } else if(iequals(attrName, "RemainingExpireTime")) {
      item.remainingExpireTime = readNumber(line, pos);
    } else if(iequals(attrName, "SavedExpireTime")) {
      item.savedExpireTime = readNumber(line, pos);
    } else if(iequals(attrName, "RemainingUses")) {
      item.remainingUses = readNumber(line, pos);
    } else {
      warning("Unknown attribute '%s'", attrName.c_str());
      // Try to skip the value
      skipWhitespace(line, pos);
      if(pos < line.size() && line[pos] == '"') {
        readQuotedString(line, pos);
      } else {
        readNumber(line, pos);
      }
    }
  }
  return true;
}

bool IOMapSec::parseItem(const std::string& line, size_t& pos, ParsedItem& item) {
  skipWhitespace(line, pos);
  if(pos >= line.size() || !std::isdigit((unsigned char)line[pos])) return false;
  item.id = (uint16_t)readNumber(line, pos);
  return parseAttributes(line, pos, item);
}

bool IOMapSec::parseItemList(const std::string& line, size_t& pos, std::vector<ParsedItem>& items) {
  while(pos < line.size()) {
    skipWhitespace(line, pos);
    if(pos >= line.size()) break;
    if(line[pos] == '}') {
      ++pos; // consume closing brace
      return true;
    }
    if(line[pos] == ',') {
      ++pos;
      skipWhitespace(line, pos);
      continue;
    }
    ParsedItem item;
    if(!parseItem(line, pos, item)) break;
    items.push_back(std::move(item));
  }
  return true;
}

Item* IOMapSec::createItemFromParsed(const ParsedItem& parsed) {
  // .sec TypeIDs are raw client/sprite IDs — translate to OTB server ID
  uint16_t clientId = parsed.id;
  uint16_t lookupId = clientId;

  // Use objects.srv for disguise resolution instead of separate disguiseMap
  auto infoIt = objectInfo.find(clientId);
  if(infoIt != objectInfo.end() && infoIt->second.isDisguise) {
    lookupId = infoIt->second.disguiseTarget;
  }

  // Find OTB server ID from client ID
  uint16_t serverId = lookupId;
  auto it = g_items.clientToServer.find(lookupId);
  if(it != g_items.clientToServer.end()) {
    serverId = it->second;
  }

  // Use objects.srv flags to decide item subclass instead of OTB
  bool srvContainer = (infoIt != objectInfo.end()) ? infoIt->second.isContainer : g_items.getItemType(serverId).isContainer();
  bool srvDoor = (infoIt != objectInfo.end()) ? infoIt->second.isDoor : g_items.getItemType(serverId).isDoor();
  bool srvTeleport = (infoIt != objectInfo.end()) ? infoIt->second.isTeleport : g_items.getItemType(serverId).isTeleport();

  Item* item;
  if(srvContainer || !parsed.children.empty()) {
    item = new Container(serverId);
  } else if(srvDoor) {
    item = new Door(serverId);
  } else if(srvTeleport) {
    item = new Teleport(serverId);
  } else {
    item = Item::Create(serverId);
  }
  if(!item) return nullptr;

  // Store original .sec TypeID (clientID) for round-trip saving
  if(lookupId != clientId) {
    item->setAttribute("sec_typeid", (int32_t)clientId);
  }

  // Amount (stackable count, fluid type)
  if(parsed.amount >= 0) item->setSubtype(parsed.amount);
  // Charges
  if(parsed.charges >= 0) item->setSubtype(parsed.charges);
  // Container liquid type (fluid containers)
  if(parsed.containerLiquidType >= 0) item->setSubtype(parsed.containerLiquidType);
  // Pool liquid type (splashes)
  if(parsed.poolLiquidType >= 0) item->setSubtype(parsed.poolLiquidType);

  // Text
  if(parsed.hasText) item->setText(parsed.text);
  // Editor → description
  if(parsed.hasEditor) item->setDescription(parsed.editor);

  // KeyNumber — store as action ID for OTBM compatibility
  if(parsed.keyNumber >= 0) item->setActionID(parsed.keyNumber);
  // KeyholeNumber — store as custom attribute
  if(parsed.keyholeNumber >= 0) item->setAttribute("keyholeid", parsed.keyholeNumber);

  // ChestQuestNumber — store as custom attribute
  if(parsed.chestQuestNumber >= 0) item->setAttribute("chestquestnumber", parsed.chestQuestNumber);

  // Door attributes — store as int32_t attribute to avoid uint8_t truncation
  if(parsed.doorLevel >= 0) {
    Door* door = item->getDoor();
    if(door) door->setDoorID(parsed.doorLevel);
    if(parsed.doorLevel > 255) item->setAttribute("sec_doorlevel", parsed.doorLevel);
  }
  if(parsed.doorQuestNumber >= 0) item->setAttribute("doorquestnumber", parsed.doorQuestNumber);
  if(parsed.doorQuestValue >= 0) item->setAttribute("doorquestvalue", parsed.doorQuestValue);

  // Teleport destination
  Teleport* teleport = item->getTeleport();
  if(teleport && parsed.absTeleportDest != INT32_MIN) {
    int x, y, z;
    UnpackAbsoluteCoordinate(parsed.absTeleportDest, x, y, z);
    teleport->setDestination(Position(x, y, z));
  }

  // Responsible
  if(parsed.responsible >= 0) item->setAttribute("responsible", parsed.responsible);
  // Expire/uses times
  if(parsed.remainingExpireTime >= 0) item->setAttribute("remainingexpiretime", parsed.remainingExpireTime);
  if(parsed.savedExpireTime >= 0) item->setAttribute("savedexpiretime", parsed.savedExpireTime);
  if(parsed.remainingUses >= 0) item->setAttribute("remaininguses", parsed.remainingUses);

  // Container contents (recursive)
  Container* container = item->getContainer();
  if(container && !parsed.children.empty()) {
    for(const auto& child : parsed.children) {
      Item* childItem = createItemFromParsed(child);
      if(childItem) container->getVector().push_back(childItem);
    }
  }

  return item;
}

// =============================================================================
// Load
// =============================================================================

bool IOMapSec::loadSectorFile(Map& map, const std::string& filepath, int sector_x, int sector_y, int floor_z) {
  std::ifstream file(filepath);
  if(!file.is_open()) {
    warning("Could not open sector file: %s", filepath.c_str());
    return false;
  }

  std::string line;
  while(std::getline(file, line)) {
    // Skip empty lines and comments
    if(line.empty() || line[0] == '#') continue;

    // Parse X-Y: prefix
    size_t pos = 0;
    // Read offset X
    if(pos >= line.size() || !std::isdigit((unsigned char)line[pos])) continue;
    int offsetX = 0;
    while(pos < line.size() && std::isdigit((unsigned char)line[pos])) {
      offsetX = offsetX * 10 + (line[pos] - '0');
      ++pos;
    }
    if(pos >= line.size() || line[pos] != '-') continue;
    ++pos;
    // Read offset Y
    int offsetY = 0;
    while(pos < line.size() && std::isdigit((unsigned char)line[pos])) {
      offsetY = offsetY * 10 + (line[pos] - '0');
      ++pos;
    }
    if(pos >= line.size() || line[pos] != ':') continue;
    ++pos;
    skipWhitespace(line, pos);

    if(offsetX < 0 || offsetX > 31 || offsetY < 0 || offsetY > 31) {
      warning("Invalid sector offset %d-%d in %s", offsetX, offsetY, filepath.c_str());
      continue;
    }

    int absX = sector_x * 32 + offsetX;
    int absY = sector_y * 32 + offsetY;
    int absZ = floor_z;

    // Parse flags and content
    uint16_t tileFlags = 0;
    std::vector<ParsedItem> items;
    bool hasContent = false;

    while(pos < line.size()) {
      skipWhitespace(line, pos);
      if(pos >= line.size()) break;

      // Check for Content=
      std::string token = readIdentifier(line, pos);
      if(token.empty()) break;

      if(iequals(token, "Refresh")) {
        tileFlags |= TILESTATE_REFRESH;
      } else if(iequals(token, "NoLogout")) {
        tileFlags |= TILESTATE_NOLOGOUT;
      } else if(iequals(token, "ProtectionZone")) {
        tileFlags |= TILESTATE_PROTECTIONZONE;
      } else if(iequals(token, "Content")) {
        skipWhitespace(line, pos);
        if(pos < line.size() && line[pos] == '=') {
          ++pos;
          skipWhitespace(line, pos);
          if(pos < line.size() && line[pos] == '{') {
            ++pos;
            parseItemList(line, pos, items);
            hasContent = true;
          }
        }
      }

      // Skip comma separator between flags
      skipWhitespace(line, pos);
      if(pos < line.size() && line[pos] == ',') {
        ++pos;
        skipWhitespace(line, pos);
      }
    }

    // Create tile if we have flags or content
    if(tileFlags == 0 && !hasContent) continue;

    Position tilePos(absX, absY, absZ);
    Tile* tile = map.allocator(map.createTileL(tilePos));

    if(tileFlags != 0) tile->setMapFlags(tileFlags);

    for(const auto& parsed : items) {
      Item* item = createItemFromParsed(parsed);
      if(!item) continue;
      // Use objects.srv Bank flag to identify ground, not OTB's isGroundTile()
      auto infoIt = objectInfo.find(parsed.id);
      bool isBank = (infoIt != objectInfo.end()) ? infoIt->second.isBank : item->isGroundTile();
      if(isBank) { delete tile->ground; tile->ground = item; }
      else { tile->items.push_back(item); }
    }

    tile->update();
    map.setTile(tilePos, tile);
  }

  return true;
}

// Check if a comma-separated flag list contains a specific flag (case-insensitive)
static bool hasFlag(const std::string& flagStr, const char* flag) {
  std::string lower;
  lower.reserve(flagStr.size());
  for(char c : flagStr) lower += std::tolower((unsigned char)c);
  std::string lowerFlag;
  for(const char* p = flag; *p; ++p) lowerFlag += std::tolower((unsigned char)*p);
  size_t pos = lower.find(lowerFlag);
  if(pos == std::string::npos) return false;
  // Ensure it's a whole word (not a substring of another flag)
  if(pos > 0 && std::isalpha((unsigned char)lower[pos - 1])) return false;
  size_t end = pos + lowerFlag.size();
  if(end < lower.size() && std::isalpha((unsigned char)lower[end])) return false;
  return true;
}

// ============================================================================
// Object config (flag/attribute names from XML)
// ============================================================================

void IOMapSec::loadObjectConfig(const std::string& dataDir) {
  objectConfig.flagNames.clear();
  objectConfig.attributeNames.clear();
  std::string filepath = dataDir + "SEC-object-attributes.xml";
  pugi::xml_document doc;
  pugi::xml_parse_result result = doc.load_file(filepath.c_str());
  if(!result) return;
  pugi::xml_node root = doc.child("sec-object-config");
  for(pugi::xml_node node = root.child("flags").child("flag"); node; node = node.next_sibling("flag")) {
    std::string name = node.attribute("name").as_string();
    if(!name.empty()) objectConfig.flagNames.push_back(name);
  }
  for(pugi::xml_node node = root.child("attributes").child("attribute"); node; node = node.next_sibling("attribute")) {
    std::string name = node.attribute("name").as_string();
    if(!name.empty()) objectConfig.attributeNames.push_back(name);
  }
}

void IOMapSec::saveObjectConfig(const std::string& dataDir) {
  std::string filepath = dataDir + "SEC-object-attributes.xml";
  pugi::xml_document doc;
  pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
  decl.append_attribute("version") = "1.0";
  decl.append_attribute("encoding") = "UTF-8";
  pugi::xml_node root = doc.append_child("sec-object-config");
  pugi::xml_node flagsNode = root.append_child("flags");
  for(const auto& f : objectConfig.flagNames) {
    pugi::xml_node n = flagsNode.append_child("flag");
    n.append_attribute("name") = f.c_str();
  }
  pugi::xml_node attrsNode = root.append_child("attributes");
  for(const auto& a : objectConfig.attributeNames) {
    pugi::xml_node n = attrsNode.append_child("attribute");
    n.append_attribute("name") = a.c_str();
  }
  doc.save_file(filepath.c_str(), "  ");
}

// ============================================================================
// Full objects.srv parser/serializer for item editor
// ============================================================================

static std::string monExtractQuoted(const std::string& s, size_t start = 0);

static void objParseFlags(const std::string& value, std::vector<std::string>& flags) {
  // Parse {Flag1,Flag2,...}
  std::string token;
  for(size_t i = 0; i < value.size(); ++i) {
    char c = value[i];
    if(c == '{' || c == '}') continue;
    if(c == ',') {
      // Trim token
      size_t s = 0; while(s < token.size() && std::isspace((unsigned char)token[s])) ++s;
      size_t e = token.size(); while(e > s && std::isspace((unsigned char)token[e-1])) --e;
      if(e > s) flags.push_back(token.substr(s, e - s));
      token.clear();
      continue;
    }
    token += c;
  }
  size_t s = 0; while(s < token.size() && std::isspace((unsigned char)token[s])) ++s;
  size_t e = token.size(); while(e > s && std::isspace((unsigned char)token[e-1])) --e;
  if(e > s) flags.push_back(token.substr(s, e - s));
}

static void objParseAttributes(const std::string& value, std::vector<std::pair<std::string, int>>& attrs) {
  // Parse {Key1=Val1,Key2=Val2,...}
  std::string token;
  for(size_t i = 0; i < value.size(); ++i) {
    char c = value[i];
    if(c == '{' || c == '}') continue;
    if(c == ',') {
      size_t eq = token.find('=');
      if(eq != std::string::npos) {
        std::string key = token.substr(0, eq);
        // Trim key
        size_t s = 0; while(s < key.size() && std::isspace((unsigned char)key[s])) ++s;
        size_t e2 = key.size(); while(e2 > s && std::isspace((unsigned char)key[e2-1])) --e2;
        key = key.substr(s, e2 - s);
        int val = std::atoi(token.c_str() + eq + 1);
        if(!key.empty()) attrs.push_back({key, val});
      }
      token.clear();
      continue;
    }
    token += c;
  }
  // Last token
  size_t eq = token.find('=');
  if(eq != std::string::npos) {
    std::string key = token.substr(0, eq);
    size_t s = 0; while(s < key.size() && std::isspace((unsigned char)key[s])) ++s;
    size_t e2 = key.size(); while(e2 > s && std::isspace((unsigned char)key[e2-1])) --e2;
    key = key.substr(s, e2 - s);
    int val = std::atoi(token.c_str() + eq + 1);
    if(!key.empty()) attrs.push_back({key, val});
  }
}

void IOMapSec::loadObjectTypes(const std::string& filepath) {
  if(objectTypesLoaded) return;
  objectTypesLoaded = true;
  objectsSrvPath = filepath;

  std::ifstream file(filepath);
  if(!file.is_open()) return;

  SecObjectType current;
  int currentId = -1;

  auto commitEntry = [&]() {
    if(currentId >= 0) {
      current.typeId = currentId;
      objectTypes[currentId] = current;
    }
  };

  std::string line;
  while(std::getline(file, line)) {
    if(line.empty() || line[0] == '#') continue;
    size_t eqPos = line.find('=');
    if(eqPos == std::string::npos) continue;

    std::string key;
    for(size_t i = 0; i < eqPos; ++i) {
      if(!std::isspace((unsigned char)line[i])) key += line[i];
    }
    std::string value = line.substr(eqPos + 1);
    size_t vs = 0; while(vs < value.size() && std::isspace((unsigned char)value[vs])) ++vs;
    value = value.substr(vs);

    if(key == "TypeID") {
      commitEntry();
      currentId = std::atoi(value.c_str());
      current = SecObjectType{};
    } else if(key == "Name") {
      current.name = monExtractQuoted(value);
    } else if(key == "Description") {
      current.description = monExtractQuoted(value);
    } else if(key == "Flags") {
      objParseFlags(value, current.flags);
    } else if(key == "Attributes") {
      objParseAttributes(value, current.attributes);
    }
  }
  commitEntry();
}

void IOMapSec::saveObjectTypes() {
  if(objectsSrvPath.empty()) return;
  std::ofstream out(objectsSrvPath);
  if(!out.is_open()) return;

  // Sort by typeId
  std::vector<int> ids;
  for(const auto& pair : objectTypes) ids.push_back(pair.first);
  std::sort(ids.begin(), ids.end());

  for(int id : ids) {
    const SecObjectType& obj = objectTypes.at(id);
    out << "\n";
    out << "TypeID      = " << obj.typeId << "\n";
    out << "Name        = \"" << obj.name << "\"\n";
    if(!obj.description.empty()) {
      out << "Description = \"" << obj.description << "\"\n";
    }
    out << "Flags       = {";
    for(size_t i = 0; i < obj.flags.size(); ++i) {
      if(i > 0) out << ",";
      out << obj.flags[i];
    }
    out << "}\n";
    out << "Attributes  = {";
    for(size_t i = 0; i < obj.attributes.size(); ++i) {
      if(i > 0) out << ",";
      out << obj.attributes[i].first << "=" << obj.attributes[i].second;
    }
    out << "}\n";
  }
}

void IOMapSec::rebuildObjectInfo() {
  objectInfo.clear();
  for(const auto& pair : objectTypes) {
    const SecObjectType& obj = pair.second;
    SecObjectInfo info;
    for(const auto& f : obj.flags) {
      std::string fl;
      for(char c : f) fl += std::tolower((unsigned char)c);
      if(fl == "bank") info.isBank = true;
      else if(fl == "bottom") info.isBottom = true;
      else if(fl == "top") info.isTop = true;
      else if(fl == "container" || fl == "chest") info.isContainer = true;
      else if(fl == "keydoor" || fl == "namedoor" || fl == "leveldoor" || fl == "questdoor") info.isDoor = true;
      else if(fl == "teleportabsolute" || fl == "teleportrelative") info.isTeleport = true;
      else if(fl == "disguise") info.isDisguise = true;
    }
    if(info.isDisguise) {
      for(const auto& attr : obj.attributes) {
        std::string ak;
        for(char c : attr.first) ak += std::tolower((unsigned char)c);
        if(ak == "disguisetarget") { info.disguiseTarget = (uint16_t)attr.second; break; }
      }
    }
    objectInfo[(uint16_t)pair.first] = info;
  }
}

void IOMapSec::loadObjectsSrv(const std::string& dataDir) {
  if(!objectInfo.empty()) return;

  std::string filepath = dataDir + "objects.srv";
  std::ifstream file(filepath);
  if(!file.is_open()) return;

  int currentTypeID = -1;
  SecObjectInfo currentInfo;

  auto commitEntry = [&]() {
    if(currentTypeID >= 0) objectInfo[(uint16_t)currentTypeID] = currentInfo;
  };

  std::string line;
  while(std::getline(file, line)) {
    if(line.empty() || line[0] == '#') continue;

    size_t eqPos = line.find('=');
    if(eqPos == std::string::npos) continue;

    std::string key;
    for(size_t i = 0; i < eqPos; ++i) {
      if(!std::isspace((unsigned char)line[i])) key += line[i];
    }

    std::string value = line.substr(eqPos + 1);

    if(key == "TypeID") {
      commitEntry();
      currentTypeID = std::atoi(value.c_str());
      currentInfo = SecObjectInfo{};
    } else if(key == "Flags") {
      if(hasFlag(value, "Bank")) currentInfo.isBank = true;
      if(hasFlag(value, "Bottom")) currentInfo.isBottom = true;
      if(hasFlag(value, "Top")) currentInfo.isTop = true;
      if(hasFlag(value, "Container") || hasFlag(value, "Chest")) currentInfo.isContainer = true;
      if(hasFlag(value, "KeyDoor") || hasFlag(value, "NameDoor") || hasFlag(value, "LevelDoor") || hasFlag(value, "QuestDoor")) currentInfo.isDoor = true;
      if(hasFlag(value, "TeleportAbsolute") || hasFlag(value, "TeleportRelative")) currentInfo.isTeleport = true;
      if(hasFlag(value, "Disguise")) currentInfo.isDisguise = true;
    } else if(key == "Attributes") {
      if(currentInfo.isDisguise) {
        std::string lower;
        for(char c : value) lower += std::tolower((unsigned char)c);
        size_t pos = lower.find("disguisetarget=");
        if(pos != std::string::npos) currentInfo.disguiseTarget = (uint16_t)std::atoi(value.c_str() + pos + 15);
      }
    }
  }
  commitEntry();
}

// Helper: extract a quoted string from text starting at pos
static std::string monExtractQuoted(const std::string& s, size_t start) {
  size_t q1 = s.find('"', start);
  if(q1 == std::string::npos) return "";
  size_t q2 = s.find('"', q1 + 1);
  if(q2 == std::string::npos) return "";
  return s.substr(q1 + 1, q2 - q1 - 1);
}

// Helper: lowercase a string
static std::string monLower(const std::string& s) {
  std::string r;
  r.reserve(s.size());
  for(char c : s) r += std::tolower((unsigned char)c);
  return r;
}

// Helper: parse an outfit tuple like (35, 0-0-0-0) or (0, 4097)
static void monParseOutfit(const std::string& value, int& outfitId, int outfitColors[4], int& outfitItemType) {
  size_t p1 = value.find('(');
  if(p1 == std::string::npos) return;
  outfitId = std::atoi(value.c_str() + p1 + 1);
  size_t comma = value.find(',', p1);
  if(comma == std::string::npos) return;
  std::string rest = value.substr(comma + 1);
  size_t rs = 0;
  while(rs < rest.size() && std::isspace((unsigned char)rest[rs])) ++rs;
  rest = rest.substr(rs);
  if(outfitId == 0) {
    outfitItemType = std::atoi(rest.c_str());
  } else {
    int ci = 0;
    size_t pos = 0;
    while(ci < 4 && pos < rest.size()) {
      outfitColors[ci] = std::atoi(rest.c_str() + pos);
      ++ci;
      size_t dash = rest.find('-', pos);
      if(dash == std::string::npos) break;
      pos = dash + 1;
    }
  }
}

static void monParseFlags(const std::string& block, uint32_t& flags) {
  // Tokenize by comma, match flag names
  std::string token;
  for(size_t i = 0; i < block.size(); ++i) {
    char c = block[i];
    if(c == '{' || c == '}') continue;
    if(c == ',') {
      std::string f = monLower(token);
      token.clear();
      // trim
      size_t s = 0; while(s < f.size() && std::isspace((unsigned char)f[s])) ++s;
      size_t e = f.size(); while(e > s && std::isspace((unsigned char)f[e-1])) --e;
      f = f.substr(s, e - s);
      if(f == "kickboxes") flags |= IOMapSec::FLAG_KICK_BOXES;
      else if(f == "kickcreatures") flags |= IOMapSec::FLAG_KICK_CREATURES;
      else if(f == "seeinvisible") flags |= IOMapSec::FLAG_SEE_INVISIBLE;
      else if(f == "unpushable") flags |= IOMapSec::FLAG_UNPUSHABLE;
      else if(f == "distancefighting") flags |= IOMapSec::FLAG_DISTANCE_FIGHTING;
      else if(f == "nosummon") flags |= IOMapSec::FLAG_NO_SUMMON;
      else if(f == "noillusion") flags |= IOMapSec::FLAG_NO_ILLUSION;
      else if(f == "noconvince") flags |= IOMapSec::FLAG_NO_CONVINCE;
      else if(f == "noburning") flags |= IOMapSec::FLAG_NO_BURNING;
      else if(f == "nopoison") flags |= IOMapSec::FLAG_NO_POISON;
      else if(f == "noenergy") flags |= IOMapSec::FLAG_NO_ENERGY;
      else if(f == "nohit") flags |= IOMapSec::FLAG_NO_HIT;
      else if(f == "nolifedrain") flags |= IOMapSec::FLAG_NO_LIFE_DRAIN;
      else if(f == "noparalyze") flags |= IOMapSec::FLAG_NO_PARALYZE;
      continue;
    }
    token += c;
  }
  // Handle last token (after last comma or before })
  if(!token.empty()) {
    std::string f = monLower(token);
    size_t s = 0; while(s < f.size() && std::isspace((unsigned char)f[s])) ++s;
    size_t e = f.size(); while(e > s && std::isspace((unsigned char)f[e-1])) --e;
    f = f.substr(s, e - s);
    if(f == "kickboxes") flags |= IOMapSec::FLAG_KICK_BOXES;
    else if(f == "kickcreatures") flags |= IOMapSec::FLAG_KICK_CREATURES;
    else if(f == "seeinvisible") flags |= IOMapSec::FLAG_SEE_INVISIBLE;
    else if(f == "unpushable") flags |= IOMapSec::FLAG_UNPUSHABLE;
    else if(f == "distancefighting") flags |= IOMapSec::FLAG_DISTANCE_FIGHTING;
    else if(f == "nosummon") flags |= IOMapSec::FLAG_NO_SUMMON;
    else if(f == "noillusion") flags |= IOMapSec::FLAG_NO_ILLUSION;
    else if(f == "noconvince") flags |= IOMapSec::FLAG_NO_CONVINCE;
    else if(f == "noburning") flags |= IOMapSec::FLAG_NO_BURNING;
    else if(f == "nopoison") flags |= IOMapSec::FLAG_NO_POISON;
    else if(f == "noenergy") flags |= IOMapSec::FLAG_NO_ENERGY;
    else if(f == "nohit") flags |= IOMapSec::FLAG_NO_HIT;
    else if(f == "nolifedrain") flags |= IOMapSec::FLAG_NO_LIFE_DRAIN;
    else if(f == "noparalyze") flags |= IOMapSec::FLAG_NO_PARALYZE;
  }
}

static void monParseSkills(const std::string& block, std::vector<IOMapSec::SecMonsterSkill>& skills) {
  // Parse (Name, v1, v2, v3, v4, v5, v6) tuples
  size_t pos = 0;
  while(pos < block.size()) {
    size_t p1 = block.find('(', pos);
    if(p1 == std::string::npos) break;
    size_t p2 = block.find(')', p1);
    if(p2 == std::string::npos) break;
    std::string tuple = block.substr(p1 + 1, p2 - p1 - 1);
    pos = p2 + 1;

    // Split by comma
    std::vector<std::string> parts;
    std::string part;
    for(char c : tuple) {
      if(c == ',') { parts.push_back(part); part.clear(); }
      else part += c;
    }
    parts.push_back(part);
    if(parts.size() < 7) continue;

    IOMapSec::SecMonsterSkill skill;
    // Trim and store name
    std::string& nm = parts[0];
    size_t s = 0; while(s < nm.size() && std::isspace((unsigned char)nm[s])) ++s;
    size_t e = nm.size(); while(e > s && std::isspace((unsigned char)nm[e-1])) --e;
    skill.name = nm.substr(s, e - s);
    skill.actual = std::atoi(parts[1].c_str());
    skill.minimum = std::atoi(parts[2].c_str());
    skill.maximum = std::atoi(parts[3].c_str());
    skill.nextLevel = std::atoi(parts[4].c_str());
    skill.factorPercent = std::atoi(parts[5].c_str());
    skill.addLevel = std::atoi(parts[6].c_str());
    skills.push_back(skill);
  }
}

static void monParseInventory(const std::string& block, std::vector<IOMapSec::SecMonsterLoot>& inventory) {
  size_t pos = 0;
  while(pos < block.size()) {
    size_t p1 = block.find('(', pos);
    if(p1 == std::string::npos) break;
    size_t p2 = block.find(')', p1);
    if(p2 == std::string::npos) break;
    std::string tuple = block.substr(p1 + 1, p2 - p1 - 1);
    pos = p2 + 1;

    std::vector<std::string> parts;
    std::string part;
    for(char c : tuple) {
      if(c == ',') { parts.push_back(part); part.clear(); }
      else part += c;
    }
    parts.push_back(part);
    if(parts.size() < 3) continue;

    IOMapSec::SecMonsterLoot loot;
    loot.itemId = std::atoi(parts[0].c_str());
    loot.maxQuantity = std::atoi(parts[1].c_str());
    loot.probabilityPerMille = std::atoi(parts[2].c_str());
    inventory.push_back(loot);
  }
}

static void monParseTalk(const std::string& block, std::vector<std::string>& talk) {
  size_t pos = 0;
  while(pos < block.size()) {
    size_t q1 = block.find('"', pos);
    if(q1 == std::string::npos) break;
    size_t q2 = block.find('"', q1 + 1);
    if(q2 == std::string::npos) break;
    talk.push_back(block.substr(q1 + 1, q2 - q1 - 1));
    pos = q2 + 1;
  }
}

static void monParseSpells(const std::string& block, std::vector<IOMapSec::SecMonsterSpell>& spells) {
  // Each spell: ShapeType (params) -> ImpactType (params) : delay
  // Split by comma at depth 0 (not inside parens)
  std::vector<std::string> entries;
  int depth = 0;
  std::string current;
  for(size_t i = 0; i < block.size(); ++i) {
    char c = block[i];
    if(c == '{') continue;
    if(c == '}') continue;
    if(c == '(') { depth++; current += c; continue; }
    if(c == ')') { depth--; current += c; continue; }
    if(c == ',' && depth == 0) {
      entries.push_back(current);
      current.clear();
      continue;
    }
    current += c;
  }
  if(!current.empty()) entries.push_back(current);

  for(const std::string& entry : entries) {
    // Find "->" separator
    size_t arrow = entry.find("->");
    if(arrow == std::string::npos) continue;
    std::string shapePart = entry.substr(0, arrow);
    std::string rest = entry.substr(arrow + 2);

    // Find ": delay" at the end
    size_t colon = rest.rfind(':');
    if(colon == std::string::npos) continue;
    std::string impactPart = rest.substr(0, colon);
    int delay = std::atoi(rest.c_str() + colon + 1);

    IOMapSec::SecMonsterSpell spell;
    spell.delay = delay;

    // Parse shape: identifier (params)
    std::string shTrimmed;
    for(char c : shapePart) if(!std::isspace((unsigned char)c) || !shTrimmed.empty()) shTrimmed += c;
    // Extract identifier
    size_t shParen = shTrimmed.find('(');
    std::string shName = (shParen != std::string::npos) ? shTrimmed.substr(0, shParen) : shTrimmed;
    // trim shName
    while(!shName.empty() && std::isspace((unsigned char)shName.back())) shName.pop_back();
    std::string shNameLower = monLower(shName);

    // Extract shape params
    std::vector<int> shParams;
    if(shParen != std::string::npos) {
      size_t shClose = shTrimmed.find(')', shParen);
      if(shClose != std::string::npos) {
        std::string pstr = shTrimmed.substr(shParen + 1, shClose - shParen - 1);
        std::string p;
        for(char c : pstr) {
          if(c == ',') { shParams.push_back(std::atoi(p.c_str())); p.clear(); }
          else p += c;
        }
        if(!p.empty()) shParams.push_back(std::atoi(p.c_str()));
      }
    }

    if(shNameLower == "actor") { spell.shape = IOMapSec::SHAPE_ACTOR; spell.shapeParamCount = 1; }
    else if(shNameLower == "victim") { spell.shape = IOMapSec::SHAPE_VICTIM; spell.shapeParamCount = 3; }
    else if(shNameLower == "origin") { spell.shape = IOMapSec::SHAPE_ORIGIN; spell.shapeParamCount = 2; }
    else if(shNameLower == "destination") { spell.shape = IOMapSec::SHAPE_DESTINATION; spell.shapeParamCount = 4; }
    else if(shNameLower == "angle") { spell.shape = IOMapSec::SHAPE_ANGLE; spell.shapeParamCount = 3; }
    else continue;

    for(int i = 0; i < 4 && i < (int)shParams.size(); ++i) spell.shapeParams[i] = shParams[i];

    // Parse impact: identifier (params) -- Outfit has nested parens ((id, colors), duration)
    std::string imTrimmed;
    for(size_t i = 0; i < impactPart.size(); ++i) {
      if(imTrimmed.empty() && std::isspace((unsigned char)impactPart[i])) continue;
      imTrimmed += impactPart[i];
    }
    // trim trailing
    while(!imTrimmed.empty() && std::isspace((unsigned char)imTrimmed.back())) imTrimmed.pop_back();

    size_t imParen = imTrimmed.find('(');
    std::string imName = (imParen != std::string::npos) ? imTrimmed.substr(0, imParen) : imTrimmed;
    while(!imName.empty() && std::isspace((unsigned char)imName.back())) imName.pop_back();
    std::string imNameLower = monLower(imName);

    if(imNameLower == "damage") { spell.impact = IOMapSec::IMPACT_DAMAGE; spell.impactParamCount = 3; }
    else if(imNameLower == "field") { spell.impact = IOMapSec::IMPACT_FIELD; spell.impactParamCount = 1; }
    else if(imNameLower == "healing") { spell.impact = IOMapSec::IMPACT_HEALING; spell.impactParamCount = 2; }
    else if(imNameLower == "speed") { spell.impact = IOMapSec::IMPACT_SPEED; spell.impactParamCount = 3; }
    else if(imNameLower == "drunken") { spell.impact = IOMapSec::IMPACT_DRUNKEN; spell.impactParamCount = 3; }
    else if(imNameLower == "strength") { spell.impact = IOMapSec::IMPACT_STRENGTH; spell.impactParamCount = 4; }
    else if(imNameLower == "outfit") { spell.impact = IOMapSec::IMPACT_OUTFIT; spell.impactParamCount = 3; }
    else if(imNameLower == "summon") { spell.impact = IOMapSec::IMPACT_SUMMON; spell.impactParamCount = 2; }
    else continue;

    // Extract impact params - handle Outfit's nested parens specially
    if(imParen != std::string::npos) {
      if(spell.impact == IOMapSec::IMPACT_OUTFIT) {
        // Format: ((outfitId, colorOrItem), duration)
        // Find the inner paren pair
        size_t innerOpen = imTrimmed.find('(', imParen + 1);
        if(innerOpen != std::string::npos) {
          size_t innerClose = imTrimmed.find(')', innerOpen);
          if(innerClose != std::string::npos) {
            std::string innerStr = imTrimmed.substr(innerOpen + 1, innerClose - innerOpen - 1);
            // Parse outfitId, colorOrItem from inner
            size_t ic = innerStr.find(',');
            if(ic != std::string::npos) {
              spell.impactParams[0] = std::atoi(innerStr.c_str()); // outfitId
              std::string colorPart = innerStr.substr(ic + 1);
              size_t cs = 0; while(cs < colorPart.size() && std::isspace((unsigned char)colorPart[cs])) ++cs;
              colorPart = colorPart.substr(cs);
              // Could be "0-0-0-0" or just a number
              spell.impactParams[1] = std::atoi(colorPart.c_str());
            }
            // Find duration after the inner close paren
            size_t outerComma = imTrimmed.find(',', innerClose);
            if(outerComma != std::string::npos) {
              spell.impactParams[2] = std::atoi(imTrimmed.c_str() + outerComma + 1);
            }
          }
        }
      } else {
        // Standard: extract comma-separated ints
        // Find matching close paren at depth
        int d = 0;
        size_t closeP = std::string::npos;
        for(size_t i = imParen; i < imTrimmed.size(); ++i) {
          if(imTrimmed[i] == '(') d++;
          else if(imTrimmed[i] == ')') { d--; if(d == 0) { closeP = i; break; } }
        }
        if(closeP != std::string::npos) {
          std::string pstr = imTrimmed.substr(imParen + 1, closeP - imParen - 1);
          std::vector<int> imParams;
          std::string p;
          for(char c : pstr) {
            if(c == ',') { imParams.push_back(std::atoi(p.c_str())); p.clear(); }
            else p += c;
          }
          if(!p.empty()) imParams.push_back(std::atoi(p.c_str()));
          for(int i = 0; i < 4 && i < (int)imParams.size(); ++i) spell.impactParams[i] = imParams[i];
        }
      }
    }

    spells.push_back(spell);
  }
}

void IOMapSec::loadMonsterTypes(const std::string& monDir) {
  if(monsterTypesLoaded) return;
  monsterTypesLoaded = true;

  for(const auto& entry : fs::directory_iterator(monDir)) {
    if(!entry.is_regular_file()) continue;
    std::string ext = entry.path().extension().string();
    for(char& c : ext) c = std::tolower((unsigned char)c);
    if(ext != ".mon") continue;

    std::ifstream file(entry.path().string());
    if(!file.is_open()) continue;

    SecMonsterType mon;
    mon.sourceFilePath = entry.path().string();

    std::string line;
    std::string pendingKey;
    std::string pendingValue;

    auto processKeyValue = [&](const std::string& key, const std::string& value) {
      if(key == "racenumber") {
        mon.raceNumber = std::atoi(value.c_str());
      } else if(key == "name") {
        mon.name = monExtractQuoted(value);
      } else if(key == "article") {
        mon.article = monExtractQuoted(value);
      } else if(key == "outfit") {
        monParseOutfit(value, mon.outfitId, mon.outfitColors, mon.outfitItemType);
      } else if(key == "corpse") {
        mon.corpse = std::atoi(value.c_str());
      } else if(key == "blood") {
        std::string bl = monLower(value);
        size_t s = 0; while(s < bl.size() && std::isspace((unsigned char)bl[s])) ++s;
        bl = bl.substr(s);
        if(bl.find("slime") == 0) mon.blood = BLOOD_SLIME;
        else if(bl.find("bones") == 0) mon.blood = BLOOD_BONES;
        else if(bl.find("fire") == 0) mon.blood = BLOOD_FIRE;
        else if(bl.find("energy") == 0) mon.blood = BLOOD_ENERGY;
        else mon.blood = BLOOD_BLOOD;
      } else if(key == "experience") {
        mon.experience = std::atoi(value.c_str());
      } else if(key == "summoncost") {
        mon.summonCost = std::atoi(value.c_str());
      } else if(key == "fleethreshold") {
        mon.fleeThreshold = std::atoi(value.c_str());
      } else if(key == "attack") {
        mon.attack = std::atoi(value.c_str());
      } else if(key == "defend") {
        mon.defend = std::atoi(value.c_str());
      } else if(key == "armor") {
        mon.armor = std::atoi(value.c_str());
      } else if(key == "poison") {
        mon.poison = std::atoi(value.c_str());
      } else if(key == "losetarget") {
        mon.loseTarget = std::atoi(value.c_str());
      } else if(key == "strategy") {
        size_t p1 = value.find('(');
        if(p1 != std::string::npos) {
          size_t p2 = value.find(')', p1);
          if(p2 != std::string::npos) {
            std::string inner = value.substr(p1 + 1, p2 - p1 - 1);
            int idx = 0;
            std::string num;
            for(char c : inner) {
              if(c == ',') { if(idx < 4) mon.strategy[idx++] = std::atoi(num.c_str()); num.clear(); }
              else num += c;
            }
            if(!num.empty() && idx < 4) mon.strategy[idx] = std::atoi(num.c_str());
          }
        }
      } else if(key == "flags") {
        monParseFlags(value, mon.flags);
      } else if(key == "skills") {
        monParseSkills(value, mon.skills);
        // Extract hitpoints for quick access
        for(const auto& sk : mon.skills) {
          if(monLower(sk.name) == "hitpoints") { mon.hitpoints = sk.actual; break; }
        }
      } else if(key == "spells") {
        monParseSpells(value, mon.spells);
      } else if(key == "inventory") {
        monParseInventory(value, mon.inventory);
      } else if(key == "talk") {
        monParseTalk(value, mon.talk);
      }
    };

    while(std::getline(file, line)) {
      if(line.empty() || line[0] == '#') {
        if(!pendingKey.empty()) pendingValue += "\n";
        continue;
      }

      // If accumulating a multiline block, append
      if(!pendingKey.empty()) {
        pendingValue += " " + line;
        if(line.find('}') != std::string::npos) {
          processKeyValue(pendingKey, pendingValue);
          pendingKey.clear();
          pendingValue.clear();
        }
        continue;
      }

      size_t eqPos = line.find('=');
      if(eqPos == std::string::npos) continue;

      std::string key;
      for(size_t i = 0; i < eqPos; ++i) {
        if(!std::isspace((unsigned char)line[i])) key += std::tolower((unsigned char)line[i]);
      }
      std::string value = line.substr(eqPos + 1);
      size_t vstart = 0;
      while(vstart < value.size() && std::isspace((unsigned char)value[vstart])) ++vstart;
      value = value.substr(vstart);

      // Check if this is a multiline block (has { but no })
      if(value.find('{') != std::string::npos && value.find('}') == std::string::npos) {
        pendingKey = key;
        pendingValue = value;
        continue;
      }

      processKeyValue(key, value);
    }

    // Handle any remaining pending block
    if(!pendingKey.empty()) {
      processKeyValue(pendingKey, pendingValue);
    }

    if(mon.raceNumber > 0 && !mon.name.empty()) {
      monsterTypes[mon.raceNumber] = mon;
    }
  }
}

// Serializer helpers
static const char* monBloodName(IOMapSec::SecBloodType bt) {
  switch(bt) {
    case IOMapSec::BLOOD_SLIME: return "Slime";
    case IOMapSec::BLOOD_BONES: return "Bones";
    case IOMapSec::BLOOD_FIRE: return "Fire";
    case IOMapSec::BLOOD_ENERGY: return "Energy";
    default: return "Blood";
  }
}

static std::string monFormatOutfit(int outfitId, const int colors[4], int itemType) {
  if(outfitId == 0) return "(" + std::to_string(outfitId) + ", " + std::to_string(itemType) + ")";
  return "(" + std::to_string(outfitId) + ", " + std::to_string(colors[0]) + "-" + std::to_string(colors[1]) + "-" + std::to_string(colors[2]) + "-" + std::to_string(colors[3]) + ")";
}

static const char* monShapeName(IOMapSec::SecSpellShape s) {
  switch(s) {
    case IOMapSec::SHAPE_ACTOR: return "Actor";
    case IOMapSec::SHAPE_VICTIM: return "Victim";
    case IOMapSec::SHAPE_ORIGIN: return "Origin";
    case IOMapSec::SHAPE_DESTINATION: return "Destination";
    case IOMapSec::SHAPE_ANGLE: return "Angle";
    default: return "Actor";
  }
}

static const char* monImpactName(IOMapSec::SecSpellImpact im) {
  switch(im) {
    case IOMapSec::IMPACT_DAMAGE: return "Damage";
    case IOMapSec::IMPACT_FIELD: return "Field";
    case IOMapSec::IMPACT_HEALING: return "Healing";
    case IOMapSec::IMPACT_SPEED: return "Speed";
    case IOMapSec::IMPACT_DRUNKEN: return "Drunken";
    case IOMapSec::IMPACT_STRENGTH: return "Strength";
    case IOMapSec::IMPACT_OUTFIT: return "Outfit";
    case IOMapSec::IMPACT_SUMMON: return "Summon";
    default: return "Damage";
  }
}

static std::string monFormatSpellShape(const IOMapSec::SecMonsterSpell& sp) {
  std::string r = monShapeName(sp.shape);
  r += " (";
  for(int i = 0; i < sp.shapeParamCount; ++i) {
    if(i > 0) r += ", ";
    r += std::to_string(sp.shapeParams[i]);
  }
  r += ")";
  return r;
}

static std::string monFormatSpellImpact(const IOMapSec::SecMonsterSpell& sp) {
  std::string r = monImpactName(sp.impact);
  r += " (";
  if(sp.impact == IOMapSec::IMPACT_OUTFIT) {
    // Nested: ((outfitId, colorOrItem), duration)
    r += "(" + std::to_string(sp.impactParams[0]) + ", " + std::to_string(sp.impactParams[1]) + "), " + std::to_string(sp.impactParams[2]);
  } else {
    for(int i = 0; i < sp.impactParamCount; ++i) {
      if(i > 0) r += ", ";
      r += std::to_string(sp.impactParams[i]);
    }
  }
  r += ")";
  return r;
}

// Pad a key name to 14 chars to match .mon file format alignment
static std::string monPadKey(const std::string& key) {
  std::string r = key;
  while(r.size() < 14) r += ' ';
  return r;
}

void IOMapSec::saveMonsterType(const SecMonsterType& mon) {
  if(mon.sourceFilePath.empty()) return;
  std::ofstream out(mon.sourceFilePath);
  if(!out.is_open()) return;

  const std::string indent = "                 "; // 17 spaces for continuation lines

  out << "\n";
  out << monPadKey("RaceNumber") << "= " << mon.raceNumber << "\n";
  out << monPadKey("Name") << "= \"" << mon.name << "\"\n";
  out << monPadKey("Article") << "= \"" << mon.article << "\"\n";
  out << monPadKey("Outfit") << "= " << monFormatOutfit(mon.outfitId, mon.outfitColors, mon.outfitItemType) << "\n";
  out << monPadKey("Corpse") << "= " << mon.corpse << "\n";
  out << monPadKey("Blood") << "= " << monBloodName(mon.blood) << "\n";
  out << monPadKey("Experience") << "= " << mon.experience << "\n";
  out << monPadKey("SummonCost") << "= " << mon.summonCost << "\n";
  out << monPadKey("FleeThreshold") << "= " << mon.fleeThreshold << "\n";
  out << monPadKey("Attack") << "= " << mon.attack << "\n";
  out << monPadKey("Defend") << "= " << mon.defend << "\n";
  out << monPadKey("Armor") << "= " << mon.armor << "\n";
  out << monPadKey("Poison") << "= " << mon.poison << "\n";
  out << monPadKey("LoseTarget") << "= " << mon.loseTarget << "\n";
  out << monPadKey("Strategy") << "= (" << mon.strategy[0] << ", " << mon.strategy[1] << ", " << mon.strategy[2] << ", " << mon.strategy[3] << ")\n";

  // Flags
  if(mon.flags != 0) {
    out << "\n";
    struct FlagEntry { uint32_t bit; const char* name; };
    static const FlagEntry flagTable[] = {
      {FLAG_KICK_BOXES, "KickBoxes"}, {FLAG_KICK_CREATURES, "KickCreatures"},
      {FLAG_SEE_INVISIBLE, "SeeInvisible"}, {FLAG_UNPUSHABLE, "Unpushable"},
      {FLAG_DISTANCE_FIGHTING, "DistanceFighting"}, {FLAG_NO_SUMMON, "NoSummon"},
      {FLAG_NO_ILLUSION, "NoIllusion"}, {FLAG_NO_CONVINCE, "NoConvince"},
      {FLAG_NO_BURNING, "NoBurning"}, {FLAG_NO_POISON, "NoPoison"},
      {FLAG_NO_ENERGY, "NoEnergy"}, {FLAG_NO_HIT, "NoHit"},
      {FLAG_NO_LIFE_DRAIN, "NoLifeDrain"}, {FLAG_NO_PARALYZE, "NoParalyze"},
    };
    std::vector<std::string> activeFlags;
    for(const auto& fe : flagTable) {
      if(mon.flags & fe.bit) activeFlags.push_back(fe.name);
    }
    out << monPadKey("Flags") << "= {" << activeFlags[0];
    for(size_t i = 1; i < activeFlags.size(); ++i) {
      out << ",\n" << indent << activeFlags[i];
    }
    out << "}\n";
  }

  // Skills
  if(!mon.skills.empty()) {
    out << "\n";
    out << monPadKey("Skills") << "= {(" << mon.skills[0].name << ", " << mon.skills[0].actual << ", " << mon.skills[0].minimum << ", " << mon.skills[0].maximum << ", " << mon.skills[0].nextLevel << ", " << mon.skills[0].factorPercent << ", " << mon.skills[0].addLevel << ")";
    for(size_t i = 1; i < mon.skills.size(); ++i) {
      const auto& sk = mon.skills[i];
      out << ",\n" << indent << "(" << sk.name << ", " << sk.actual << ", " << sk.minimum << ", " << sk.maximum << ", " << sk.nextLevel << ", " << sk.factorPercent << ", " << sk.addLevel << ")";
    }
    out << "}\n";
  }

  // Spells
  if(!mon.spells.empty()) {
    out << "\n";
    out << monPadKey("Spells") << "= {" << monFormatSpellShape(mon.spells[0]) << " -> " << monFormatSpellImpact(mon.spells[0]) << " : " << mon.spells[0].delay;
    for(size_t i = 1; i < mon.spells.size(); ++i) {
      out << ",\n" << indent << monFormatSpellShape(mon.spells[i]) << " -> " << monFormatSpellImpact(mon.spells[i]) << " : " << mon.spells[i].delay;
    }
    out << "}\n";
  }

  // Inventory
  if(!mon.inventory.empty()) {
    out << "\n";
    out << monPadKey("Inventory") << "= {(" << mon.inventory[0].itemId << ", " << mon.inventory[0].maxQuantity << ", " << mon.inventory[0].probabilityPerMille << ")";
    for(size_t i = 1; i < mon.inventory.size(); ++i) {
      const auto& it = mon.inventory[i];
      out << ",\n" << indent << "(" << it.itemId << ", " << it.maxQuantity << ", " << it.probabilityPerMille << ")";
    }
    out << "}\n";
  }

  // Talk
  if(!mon.talk.empty()) {
    out << "\n";
    out << monPadKey("Talk") << "= {\"" << mon.talk[0] << "\"";
    for(size_t i = 1; i < mon.talk.size(); ++i) {
      out << ",\n" << indent << "\"" << mon.talk[i] << "\"";
    }
    out << "}\n";
  }
}

void IOMapSec::saveAllMonsterTypes() {
  for(const auto& pair : monsterTypes) {
    saveMonsterType(pair.second);
  }
}

void IOMapSec::loadMonsterDb(const std::string& filepath) {
  spawnEntries.clear();
  std::ifstream file(filepath);
  if(!file.is_open()) return;

  std::string line;
  while(std::getline(file, line)) {
    if(line.empty()) continue;
    // Skip comment lines
    size_t first = line.find_first_not_of(" \t");
    if(first == std::string::npos || line[first] == '#') continue;

    // Data line: Race X Y Z Radius Amount Regen
    SecSpawnEntry entry;
    if(sscanf(line.c_str(), " %d %d %d %d %d %d %d", &entry.raceNumber, &entry.x, &entry.y, &entry.z, &entry.radius, &entry.amount, &entry.regen) == 7) {
      spawnEntries.push_back(entry);
    }
  }
}

void IOMapSec::loadMapDat(const std::string& filepath) {
  mapBounds = SecMapBounds();
  std::ifstream file(filepath);
  if(!file.is_open()) return;

  // Only the sector extents are of interest here; marks and start positions are ignored.
  int sxMin = -1, sxMax = -1, syMin = -1, syMax = -1, szMin = -1, szMax = -1;
  std::string line;
  while(std::getline(file, line)) {
    if(line.empty() || line[0] == '#') continue;
    char key[32];
    int value;
    if(sscanf(line.c_str(), " %31[A-Za-z] = %d", key, &value) != 2) continue;
    if(iequals(key, "SectorXMin")) sxMin = value;
    else if(iequals(key, "SectorXMax")) sxMax = value;
    else if(iequals(key, "SectorYMin")) syMin = value;
    else if(iequals(key, "SectorYMax")) syMax = value;
    else if(iequals(key, "SectorZMin")) szMin = value;
    else if(iequals(key, "SectorZMax")) szMax = value;
  }

  if(sxMin < 0 || syMin < 0 || sxMax < sxMin || syMax < syMin) return;

  // Sectors are 32x32 tiles, matching the sector_x * 32 + offset arithmetic in loadSectorFile.
  mapBounds.minX = sxMin * 32;
  mapBounds.maxX = sxMax * 32 + 31;
  mapBounds.minY = syMin * 32;
  mapBounds.maxY = syMax * 32 + 31;
  mapBounds.minZ = szMin < 0 ? rme::MapMinLayer : std::max(szMin, rme::MapMinLayer);
  mapBounds.maxZ = szMax < 0 ? rme::MapMaxLayer : std::min(szMax, rme::MapMaxLayer);
  mapBounds.valid = true;
}

void IOMapSec::loadHouseAreas(const std::string& filepath) {
  houseAreas.clear();
  std::ifstream file(filepath);
  if(!file.is_open()) return;

  std::string line;
  while(std::getline(file, line)) {
    if(line.empty() || line[0] == '#') continue;
    // Format: Area = (areaId,"areaName",sqmPrice,depotNr)
    size_t paren = line.find('(');
    if(paren == std::string::npos) continue;
    SecHouseArea area;
    area.areaId = std::atoi(line.c_str() + paren + 1);
    size_t q1 = line.find('"', paren);
    size_t q2 = line.find('"', q1 + 1);
    if(q1 != std::string::npos && q2 != std::string::npos) area.name = line.substr(q1 + 1, q2 - q1 - 1);
    size_t comma2 = line.find(',', q2 + 1);
    if(comma2 != std::string::npos) area.sqmPrice = std::atoi(line.c_str() + comma2 + 1);
    size_t comma3 = line.find(',', comma2 + 1);
    if(comma3 != std::string::npos) area.depotNr = std::atoi(line.c_str() + comma3 + 1);
    if(area.areaId > 0) houseAreas[area.areaId] = area;
  }
}

void IOMapSec::loadHouses(Map& map, const std::string& filepath) {
  std::ifstream file(filepath);
  if(!file.is_open()) return;

  std::string fullContent((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

  // Split by "ID = " to get each house block
  size_t pos = 0;
  while(pos < fullContent.size()) {
    // Find next "ID = "
    size_t idPos = fullContent.find("ID = ", pos);
    if(idPos == std::string::npos) break;

    uint32_t houseId = (uint32_t)std::atoi(fullContent.c_str() + idPos + 5);
    if(houseId == 0) { pos = idPos + 5; continue; }

    // Find the extent of this house entry (until next "ID = " or EOF)
    size_t nextId = fullContent.find("\nID = ", idPos + 1);
    if(nextId == std::string::npos) nextId = fullContent.find("\r\nID = ", idPos + 1);
    std::string block = (nextId != std::string::npos) ? fullContent.substr(idPos, nextId - idPos) : fullContent.substr(idPos);
    pos = (nextId != std::string::npos) ? nextId + 1 : fullContent.size();

    House* house = newd House(map);
    house->id = houseId;

    // Parse Name
    size_t namePos = block.find("Name = ");
    if(namePos != std::string::npos) {
      size_t q1 = block.find('"', namePos);
      size_t q2 = block.find('"', q1 + 1);
      if(q1 != std::string::npos && q2 != std::string::npos) house->name = block.substr(q1 + 1, q2 - q1 - 1);
    }

    // Parse Description
    size_t descPos = block.find("Description = ");
    if(descPos != std::string::npos) {
      size_t q1 = block.find('"', descPos);
      size_t q2 = block.find('"', q1 + 1);
      if(q1 != std::string::npos && q2 != std::string::npos) house->description = block.substr(q1 + 1, q2 - q1 - 1);
    }

    // Parse RentOffset
    size_t rentPos = block.find("RentOffset = ");
    if(rentPos != std::string::npos) house->rentOffset = std::atoi(block.c_str() + rentPos + 13);

    // Parse Area
    size_t areaPos = block.find("Area = ");
    if(areaPos != std::string::npos) {
      // Make sure this isn't "HouseArea" or similar — check it's at line start or after newline
      house->areaId = std::atoi(block.c_str() + areaPos + 7);
      // Map area to town ID
      house->townid = house->areaId;
    }

    // Parse GuildHouse
    size_t ghPos = block.find("GuildHouse = ");
    if(ghPos != std::string::npos) {
      std::string ghVal = block.substr(ghPos + 13, 4);
      for(char& c : ghVal) c = std::tolower((unsigned char)c);
      house->guildhall = (ghVal.find("true") == 0);
    }

    // Parse Exit = [x,y,z]
    size_t exitPos = block.find("Exit = ");
    if(exitPos != std::string::npos) {
      size_t br1 = block.find('[', exitPos);
      if(br1 != std::string::npos) {
        int ex, ey, ez;
        if(sscanf(block.c_str() + br1, "[%d,%d,%d]", &ex, &ey, &ez) == 3) house->setExit(Position(ex, ey, ez));
      }
    }

    // Parse Center = [x,y,z]
    size_t centerPos = block.find("Center = ");
    if(centerPos != std::string::npos) {
      size_t br1 = block.find('[', centerPos);
      if(br1 != std::string::npos) {
        int cx, cy, cz;
        if(sscanf(block.c_str() + br1, "[%d,%d,%d]", &cx, &cy, &cz) == 3) house->setCenter(Position(cx, cy, cz));
      }
    }

    // Parse Fields = {[x,y,z],[x,y,z],...}
    size_t fieldsPos = block.find("Fields = ");
    if(fieldsPos != std::string::npos) {
      size_t fpos = block.find('{', fieldsPos);
      while(fpos < block.size()) {
        size_t br = block.find('[', fpos);
        if(br == std::string::npos) break;
        size_t brEnd = block.find(']', br);
        if(brEnd == std::string::npos) break;
        int fx, fy, fz;
        if(sscanf(block.c_str() + br, "[%d,%d,%d]", &fx, &fy, &fz) == 3) {
          Position fieldPos(fx, fy, fz);
          Tile* tile = map.getTile(fieldPos);
          if(!tile) {
            tile = map.allocator(map.createTileL(fieldPos));
            map.setTile(fieldPos, tile);
          }
          tile->setHouse(house);
          house->addTile(tile);
        }
        fpos = brEnd + 1;
        if(fpos < block.size() && block[fpos] == '}') break;
      }
    }

    // Calculate rent from area
    auto areaIt = houseAreas.find(house->areaId);
    if(areaIt != houseAreas.end()) {
      house->rent = areaIt->second.sqmPrice * (int)house->size() + house->rentOffset;
    } else {
      house->rent = house->rentOffset;
    }

    map.houses.addHouse(house);
  }
}

void IOMapSec::saveHouses(Map& map, const std::string& filepath) {
  std::string buf;
  for(auto it = map.houses.begin(); it != map.houses.end(); ++it) {
    House* house = it->second;
    if(!buf.empty()) buf += "\r\n";
    buf += "ID = "; char tmp[32]; snprintf(tmp, sizeof(tmp), "%u", house->id); buf += tmp;
    buf += "\r\nName = \""; buf += house->name; buf += "\"";
    buf += "\r\nDescription = \""; buf += house->description; buf += "\"";
    buf += "\r\nRentOffset = "; snprintf(tmp, sizeof(tmp), "%d", house->rentOffset); buf += tmp;
    buf += "\r\nArea = "; snprintf(tmp, sizeof(tmp), "%d", house->areaId); buf += tmp;
    buf += "\r\nGuildHouse = "; buf += house->guildhall ? "true" : "false";
    buf += "\r\nExit = ["; snprintf(tmp, sizeof(tmp), "%d,%d,%d", house->getExit().x, house->getExit().y, house->getExit().z); buf += tmp; buf += "]";
    buf += "\r\nCenter = ["; snprintf(tmp, sizeof(tmp), "%d,%d,%d", house->getCenter().x, house->getCenter().y, house->getCenter().z); buf += tmp; buf += "]";
    buf += "\r\nFields = {";
    bool firstField = true;
    for(const Position& fieldPos : house->getTiles()) {
      if(!firstField) buf += ",";
      snprintf(tmp, sizeof(tmp), "[%d,%d,%d]", fieldPos.x, fieldPos.y, fieldPos.z);
      buf += tmp;
      firstField = false;
    }
    buf += "}\r\n";
  }
  FILE* f = fopen(filepath.c_str(), "wb");
  if(f) { fwrite(buf.data(), 1, buf.size(), f); fclose(f); }
}

bool IOMapSec::loadMap(Map& map, const FileName& identifier) {
  wxFileName fn(identifier);
  std::string dirPath = nstr(fn.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME));

  if(dirPath.empty()) {
    error("Could not determine directory from file: %s", nstr(identifier.GetFullPath()).c_str());
    return false;
  }

  // Derive root directory: the .sec file is in root/map/
  // Root is the parent of the directory containing the .sec file
  fs::path mapDirPath = fs::path(dirPath).parent_path();
  std::string rootDir = mapDirPath.parent_path().string() + "/";
  std::string mapDir = rootDir + "map/";
  std::string datDir = rootDir + "dat/";
  std::string monDir = rootDir + "mon/";

  // If root/map/ doesn't exist, fall back to using dirPath directly as the map dir
  if(!fs::is_directory(mapDir)) {
    mapDir = dirPath;
    rootDir = dirPath;
    datDir = "";
    monDir = "";
  }

  // Scan map directory for .sec files
  int fileCount = 0;
  int maxSectorX = 0, maxSectorY = 0;

  struct SectorInfo {
    std::string filepath;
    int sx, sy, sz;
  };
  std::vector<SectorInfo> sectors;

  try {
    for(const auto& entry : fs::directory_iterator(mapDir)) {
      if(!entry.is_regular_file()) continue;
      std::string fname = entry.path().filename().string();
      int sx, sy, sz;
      if(sscanf(fname.c_str(), "%d-%d-%d.sec", &sx, &sy, &sz) == 3) {
        sectors.push_back({entry.path().string(), sx, sy, sz});
        if(sx > maxSectorX) maxSectorX = sx;
        if(sy > maxSectorY) maxSectorY = sy;
        ++fileCount;
      }
    }
  } catch(const std::exception& e) {
    error("Error scanning directory: %s", e.what());
    return false;
  }

  if(fileCount == 0) {
    error("No .sec files found in directory: %s", mapDir.c_str());
    return false;
  }

  // Set map dimensions
  map.width = (maxSectorX + 1) * 32;
  map.height = (maxSectorY + 1) * 32;

  // Build clientID -> serverID reverse map for .sec TypeID translation
  if(g_items.clientToServer.empty()) {
    for(uint16_t sid = g_items.getMinID(); sid <= g_items.getMaxID(); ++sid) {
      const ItemType& t = g_items.getItemType(sid);
      if(t.id != 0 && t.clientID != 0) {
        g_items.clientToServer[t.clientID] = sid;
      }
    }
  }

  // Load map geometry from root/dat/map.dat. Reset first: these statics outlive a single map load.
  mapBounds = SecMapBounds();
  if(!datDir.empty() && fs::exists(datDir + "map.dat")) {
    loadMapDat(datDir + "map.dat");
  }

  // Load objects.srv from root/dat/
  if(!datDir.empty() && fs::exists(datDir + "objects.srv")) {
    loadObjectsSrv(datDir);
    loadObjectTypes(datDir + "objects.srv");
    std::string rmeDataDir = nstr(g_gui.getFoundDataDirectory()) + "/";
    loadObjectConfig(rmeDataDir);
    g_materials.createOtherTileset();
    Tileset* dt = g_materials.tilesets["Disguise"];
    if(dt) {
      const TilesetCategory* dtc = dt->getCategory(TILESET_RAW);
      if(dtc && dtc->size() > 0) {
        PaletteWindow* pal = g_gui.GetPalette();
        if(pal) pal->AddRAWTilesetPage("Disguise", dtc);
      }
    }
  }
  // Load monster types from root/mon/
  if(!monDir.empty() && fs::is_directory(monDir)) {
    loadMonsterTypes(monDir);
    // Register monsters in the creature database
    for(auto& pair : monsterTypes) {
      const SecMonsterType& mon = pair.second;
      if(g_creatures[mon.name] != nullptr) continue;
      Outfit outfit;
      outfit.lookType = mon.outfitId;
      if(mon.outfitId == 0) {
        outfit.lookItem = mon.outfitItemType;
      } else {
        outfit.lookHead = mon.outfitColors[0];
        outfit.lookBody = mon.outfitColors[1];
        outfit.lookLegs = mon.outfitColors[2];
        outfit.lookFeet = mon.outfitColors[3];
      }
      g_creatures.addCreatureType(mon.name, false, outfit);
    }
    g_materials.createOtherTileset();
    g_gui.RebuildPalettes();
  }

  // Load monster.db spawn database
  if(!datDir.empty()) {
    std::string monsterDbPath = datDir + "monster.db";
    if(fs::exists(monsterDbPath)) loadMonsterDb(monsterDbPath);
  }

  // Load house areas metadata (needed before houses)
  if(!datDir.empty()) {
    std::string houseAreasPath = datDir + "houseareas.dat";
    if(fs::exists(houseAreasPath)) loadHouseAreas(houseAreasPath);
  }

  g_gui.SetLoadDone(0, "Loading sector files...");

  int loaded = 0;
  for(const auto& sec : sectors) {
    if(!loadSectorFile(map, sec.filepath, sec.sx, sec.sy, sec.sz)) {
      warning("Failed to load sector file: %s", sec.filepath.c_str());
    }
    ++loaded;
    g_gui.SetLoadDone((int)(100 * loaded / fileCount));
  }

  // Load houses after sector files (so tiles exist)
  if(!datDir.empty()) {
    std::string housesPath = datDir + "houses.dat";
    if(fs::exists(housesPath)) loadHouses(map, housesPath);
  }

  // Place monster spawns on the map from monster.db
  if(!spawnEntries.empty()) {
    int placedSpawns = 0;
    for(const auto& entry : spawnEntries) {
      // Look up creature name from race number
      auto monIt = monsterTypes.find(entry.raceNumber);
      if(monIt == monsterTypes.end()) continue;
      const std::string& creatureName = monIt->second.name;

      Position pos(entry.x, entry.y, entry.z);

      // Get or create tile at spawn position
      Tile* tile = map.getTile(pos);
      if(!tile) {
        tile = map.allocator(map.createTileL(pos));
        map.setTile(pos, tile);
      }

      // Create spawn on tile if none exists
      if(!tile->spawn) {
        // Visual radius = sqrt(SEC radius), SEC radius stored for round-trip
        int visualRadius = (int)std::sqrt((double)entry.radius);
        if(visualRadius < 1) visualRadius = 1;
        Spawn* spawn = newd Spawn(visualRadius);
        spawn->setSecRadius(entry.radius);
        spawn->setAmount(entry.amount);
        tile->spawn = spawn;
        map.addSpawn(tile);
      }

      // Place creature if tile doesn't already have one
      if(!tile->creature) {
        CreatureType* type = g_creatures[creatureName];
        if(!type) type = g_creatures.addMissingCreatureType(creatureName, false);
        Creature* creature = newd Creature(type);
        creature->setSpawnTime(entry.regen);
        creature->setRaceID(entry.raceNumber);
        tile->creature = creature;
        ++placedSpawns;
      }
    }
  }

  return true;
}

// =============================================================================
// Writer helpers
// =============================================================================

static void appendInt(std::string& buf, int val) {
  char tmp[16];
  snprintf(tmp, sizeof(tmp), "%d", val);
  buf += tmp;
}

static void escapeString(std::string& buf, const std::string& str) {
  buf += '"';
  for(char c : str) {
    if(c == '"') { buf += "\\\""; }
    else if(c == '\\') { buf += "\\\\"; }
    else if(c == '\n') { buf += "\\n"; }
    else buf += c;
  }
  buf += '"';
}

void IOMapSec::writeItemAttributes(std::string& buf, Item* item) {
  const ItemType& type = g_items.getItemType(item->getID());

  if(type.stackable && item->getSubtype() > 0) { buf += " Amount="; appendInt(buf, item->getSubtype()); }
  if(item->isCharged() && item->getSubtype() > 0 && !type.stackable) { buf += " Charges="; appendInt(buf, item->getSubtype()); }
  if(type.isSplash() && item->getSubtype() > 0) { buf += " PoolLiquidType="; appendInt(buf, item->getSubtype()); }
  else if(type.isFluidContainer() && item->getSubtype() > 0) { buf += " ContainerLiquidType="; appendInt(buf, item->getSubtype()); }

  uint16_t aid = item->getActionID();
  if(aid > 0) { buf += " KeyNumber="; appendInt(buf, aid); }

  const int32_t* keyholeId = item->getIntegerAttribute("keyholeid");
  if(keyholeId && *keyholeId > 0) { buf += " KeyholeNumber="; appendInt(buf, *keyholeId); }

  const int32_t* chestQuest = item->getIntegerAttribute("chestquestnumber");
  if(chestQuest && *chestQuest > 0) { buf += " ChestQuestNumber="; appendInt(buf, *chestQuest); }

  const int32_t* secDoorLevel = item->getIntegerAttribute("sec_doorlevel");
  Door* door = item->getDoor();
  if(secDoorLevel && *secDoorLevel > 0) { buf += " Level="; appendInt(buf, *secDoorLevel); }
  else if(door && door->getDoorID() > 0) { buf += " Level="; appendInt(buf, door->getDoorID()); }
  const int32_t* doorQuest = item->getIntegerAttribute("doorquestnumber");
  if(doorQuest && *doorQuest > 0) { buf += " DoorQuestNumber="; appendInt(buf, *doorQuest); }
  const int32_t* doorQuestVal = item->getIntegerAttribute("doorquestvalue");
  if(doorQuestVal && *doorQuestVal > 0) { buf += " DoorQuestValue="; appendInt(buf, *doorQuestVal); }

  std::string text = item->getText();
  if(!text.empty()) { buf += " String="; escapeString(buf, text); }

  std::string desc = item->getDescription();
  if(!desc.empty()) { buf += " Editor="; escapeString(buf, desc); }

  Teleport* teleport = item->getTeleport();
  if(teleport && teleport->hasDestination()) {
    const Position& dest = teleport->getDestination();
    buf += " AbsTeleportDestination=";
    appendInt(buf, PackAbsoluteCoordinate(dest.x, dest.y, dest.z));
  }

  const int32_t* resp = item->getIntegerAttribute("responsible");
  if(resp && *resp > 0) { buf += " Responsible="; appendInt(buf, *resp); }
  const int32_t* expTime = item->getIntegerAttribute("remainingexpiretime");
  if(expTime && *expTime > 0) { buf += " RemainingExpireTime="; appendInt(buf, *expTime); }
  const int32_t* savedExp = item->getIntegerAttribute("savedexpiretime");
  if(savedExp && *savedExp > 0) { buf += " SavedExpireTime="; appendInt(buf, *savedExp); }
  const int32_t* remUses = item->getIntegerAttribute("remaininguses");
  if(remUses && *remUses > 0) { buf += " RemainingUses="; appendInt(buf, *remUses); }
}

void IOMapSec::writeItem(std::string& buf, Item* item, bool first) {
  if(!first) buf += ", ";
  const int32_t* secTypeId = item->getIntegerAttribute("sec_typeid");
  if(secTypeId) { appendInt(buf, *secTypeId); }
  else { appendInt(buf, item->getClientID()); }
  writeItemAttributes(buf, item);

  Container* container = item->getContainer();
  if(container && container->getItemCount() > 0) {
    buf += " Content={";
    bool childFirst = true;
    for(Item* child : container->getVector()) {
      writeItem(buf, child, childFirst);
      childFirst = false;
    }
    buf += "}";
  }
}

// =============================================================================
// Save
// =============================================================================

static void writeTile(std::string& buf, IOMapSec* saver, Tile* tile, int x, int y) {
  uint16_t flags = tile->getMapFlags() & (TILESTATE_REFRESH | TILESTATE_NOLOGOUT | TILESTATE_PROTECTIONZONE);
  bool hasItems = tile->ground || !tile->items.empty();
  if(flags == 0 && !hasItems) return;

  appendInt(buf, x);
  buf += '-';
  appendInt(buf, y);
  buf += ": ";

  if(flags & TILESTATE_REFRESH) { buf += "Refresh, "; }
  if(flags & TILESTATE_NOLOGOUT) { buf += "NoLogout, "; }
  if(flags & TILESTATE_PROTECTIONZONE) { buf += "ProtectionZone, "; }

  if(hasItems) {
    buf += "Content={";
    bool first = true;
    if(tile->ground) { saver->writeItem(buf, tile->ground, first); first = false; }
    for(Item* item : tile->items) { saver->writeItem(buf, item, first); first = false; }
    buf += "}";
  }

  buf += '\n';
}

bool IOMapSec::saveMap(Map& map, const FileName& identifier) {
  wxFileName fn(identifier);
  std::string dirPath = nstr(fn.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME));

  if(dirPath.empty()) {
    error("Could not determine directory from file: %s", nstr(identifier.GetFullPath()).c_str());
    return false;
  }

  // Collect all tiles into a flat vector, then sort by sector
  struct TileEntry {
    uint16_t sx, sy, ox, oy;
    uint8_t sz;
    Tile* tile;
  };
  std::vector<TileEntry> entries;
  entries.reserve(map.getTileCount() > 0 ? map.getTileCount() : 500000);

  MapIterator it = map.begin();
  MapIterator end = map.end();
  while(it != end) {
    Tile* tile = (*it)->get();
    if(tile) {
      const Position& pos = tile->getPosition();
      entries.push_back({(uint16_t)(pos.x / 32), (uint16_t)(pos.y / 32), (uint16_t)(pos.x % 32), (uint16_t)(pos.y % 32), (uint8_t)pos.z, tile});
    }
    ++it;
  }

  if(entries.empty()) {
    error("Map is empty, nothing to save.");
    return false;
  }

  // Sort by (sx, sy, sz, ox, oy) so tiles in the same sector are contiguous and ordered
  std::sort(entries.begin(), entries.end(), [](const TileEntry& a, const TileEntry& b) {
    if(a.sx != b.sx) return a.sx < b.sx;
    if(a.sy != b.sy) return a.sy < b.sy;
    if(a.sz != b.sz) return a.sz < b.sz;
    if(a.ox != b.ox) return a.ox < b.ox;
    return a.oy < b.oy;
  });

  std::string buf;
  buf.reserve(64 * 1024);
  char filename[256];
  size_t i = 0;
  size_t total = entries.size();

  while(i < total) {
    uint16_t sx = entries[i].sx, sy = entries[i].sy;
    uint8_t sz = entries[i].sz;

    snprintf(filename, sizeof(filename), "%s%04d-%04d-%02d.sec", dirPath.c_str(), sx, sy, sz);

    buf.clear();

    while(i < total && entries[i].sx == sx && entries[i].sy == sy && entries[i].sz == sz) {
      writeTile(buf, this, entries[i].tile, entries[i].ox, entries[i].oy);
      ++i;
    }

    FILE* f = fopen(filename, "wb");
    if(f) { fwrite(buf.data(), 1, buf.size(), f); fclose(f); }
  }

  // Save monster.db — derive root/dat/ from save path
  fs::path saveDirPath = fs::path(dirPath).parent_path();
  std::string saveRootDir = saveDirPath.parent_path().string() + "/";
  std::string saveDatDir = saveRootDir + "dat/";
  if(fs::is_directory(saveDatDir)) {
    // Collect all spawn entries from the map
    struct SaveSpawnEntry {
      int race, x, y, z, radius, amount, regen;
      uint16_t sx, sy;
      uint8_t sz;
    };
    std::vector<SaveSpawnEntry> saveEntries;

    MapIterator sit = map.begin();
    MapIterator send = map.end();
    while(sit != send) {
      Tile* tile = (*sit)->get();
      if(tile && tile->spawn && tile->creature && tile->creature->getRaceID() > 0) {
        const Position& pos = tile->getPosition();
        int radius = tile->spawn->getSecRadius() > 0 ? tile->spawn->getSecRadius() : tile->spawn->getSize();
        int amount = tile->spawn->getAmount();
        int regen = tile->creature->getSpawnTime();
        saveEntries.push_back({tile->creature->getRaceID(), pos.x, pos.y, pos.z, radius, amount, regen, (uint16_t)(pos.x / 32), (uint16_t)(pos.y / 32), (uint8_t)pos.z});
      }
      ++sit;
    }

    // Sort by sector
    std::sort(saveEntries.begin(), saveEntries.end(), [](const SaveSpawnEntry& a, const SaveSpawnEntry& b) {
      if(a.sx != b.sx) return a.sx < b.sx;
      if(a.sy != b.sy) return a.sy < b.sy;
      return a.sz < b.sz;
    });

    // Write monster.db
    std::string dbBuf;
    dbBuf += "# Race     X     Y  Z Radius Amount Regen.\n";
    uint16_t lastSx = 0xFFFF, lastSy = 0xFFFF;
    uint8_t lastSz = 0xFF;
    char line[256];
    for(auto& e : saveEntries) {
      if(e.sx != lastSx || e.sy != lastSy || e.sz != lastSz) {
        snprintf(line, sizeof(line), "\n# ====== %04d,%04d,%02d ====================\n", e.sx, e.sy, e.sz);
        dbBuf += line;
        lastSx = e.sx; lastSy = e.sy; lastSz = e.sz;
      }
      snprintf(line, sizeof(line), "%5d %5d %5d %2d %5d %6d %4d\n", e.race, e.x, e.y, e.z, e.radius, e.amount, e.regen);
      dbBuf += line;
    }

    std::string monsterDbPath = saveDatDir + "monster.db";
    FILE* dbFile = fopen(monsterDbPath.c_str(), "wb");
    if(dbFile) { fwrite(dbBuf.data(), 1, dbBuf.size(), dbFile); fclose(dbFile); }
  }

  // Save houses.dat
  if(fs::is_directory(saveDatDir) && map.houses.count() > 0) {
    saveHouses(map, saveDatDir + "houses.dat");
  }

  return true;
}
