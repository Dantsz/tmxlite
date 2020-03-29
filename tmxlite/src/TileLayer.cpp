/*********************************************************************
Matt Marchant 2016
http://trederia.blogspot.com

tmxlite - Zlib license.

This software is provided 'as-is', without any express or
implied warranty. In no event will the authors be held
liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute
it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented;
you must not claim that you wrote the original software.
If you use this software in a product, an acknowledgment
in the product documentation would be appreciated but
is not required.

2. Altered source versions must be plainly marked as such,
and must not be misrepresented as being the original software.

3. This notice may not be removed or altered from any
source distribution.
*********************************************************************/

#include <tmxlite/FreeFuncs.hpp>
#include <tmxlite/TileLayer.hpp>
#include "detail/pugixml.hpp"
#include <tmxlite/detail/Log.hpp>

#include <sstream>

using namespace tmx;

TileLayer::TileLayer(std::size_t tileCount)
    : m_tileCount (tileCount)
{
    m_tiles.reserve(tileCount);
}

//public
void TileLayer::parse(const pugi::xml_node& node,bool is_infinite)
{
    std::string attribName = node.name();
    if (attribName != "layer")
    {
        Logger::log("node not a layer node, skipped parsing", Logger::Type::Error);
        return;
    }

    setName(node.attribute("name").as_string());
    setOpacity(node.attribute("opacity").as_float(1.f));
    setVisible(node.attribute("visible").as_bool(true));
    setOffset(node.attribute("offsetx").as_int(), node.attribute("offsety").as_int());

    for (const auto& child : node.children())
    {
        attribName = child.name();
        if (attribName == "data")
        {

            attribName = child.attribute("encoding").as_string();
            if (attribName == "base64")
            {
                if (!is_infinite) parseBase64(child);
                else parseBase64_infinite(child);
            }
            else if (attribName == "csv")
            {
                if (!is_infinite) parseCSV(child);
                else parseCVS_infinite(child);
            }
            else
            {
                parseUnencoded(child);
            }

        }
        else if (attribName == "properties")
        {
            for (const auto& p : child.children())
            {
                addProperty(p);
            }
        }
    }

}

const std::vector<TileLayer::Tile>& tmx::TileLayer::getTiles() const
{
    if (is_infinite)
    {
        tmx::Logger::log("The map is not infinite, cannot get the tiles, use getChunks() instead to the get the data as chunks", tmx::Logger::Type::Error);
    }
    return m_tiles;
}

const std::vector<TileLayer::Chunk>& tmx::TileLayer::getChunks() const
{
    if (!is_infinite)
    {
        tmx::Logger::log("The map is not infinite, cannot get the chunks , use getTiles to get the whole layer as a vector", tmx::Logger::Type::Error);
    }
    return m_chunks; 
}

//private
void TileLayer::parseBase64(const pugi::xml_node& node)
{
    is_infinite = false;
    std::string data = node.text().as_string();
    if (data.empty())
    {
        Logger::log("Layer " + getName() + " has no layer data. Layer skipped.", Logger::Type::Error);
        return;
    }

    //using a string stream we can remove whitespace such as tabs
    std::stringstream ss;
    ss << data;
    ss >> data;
    data = base64_decode(data);

    std::size_t expectedSize = m_tileCount * 4; //4 bytes per tile
    std::vector<unsigned char> byteData;
    byteData.reserve(expectedSize);

    if (node.attribute("compression"))
    {
        //unzip
        std::size_t dataSize = data.length() * sizeof(unsigned char);
        if (!decompress(data.c_str(), byteData, dataSize, expectedSize))
        {
            LOG("Failed to decompress layer data, node skipped.", Logger::Type::Error);
            return;
        }
    }
    else
    {
        byteData.insert(byteData.end(), data.begin(), data.end());
    }

    //data stream is in bytes so whe need to OR into 32 bit values
    std::vector<std::uint32_t> IDs;
    IDs.reserve(m_tileCount);
    for (auto i = 0u; i < expectedSize - 3u; i += 4u)
    {
        std::uint32_t id = byteData[i] | byteData[i + 1] << 8 | byteData[i + 2] << 16 | byteData[i + 3] << 24;
        IDs.push_back(id);
    }
    createTiles(IDs);
}

void TileLayer::parseCSV(const pugi::xml_node& node)
{
    is_infinite = false;
    std::string data = node.text().as_string();
    if (data.empty())
    {
        Logger::log("Layer " + getName() + " has no layer data. Layer skipped.", Logger::Type::Error);
        return;
    }

    std::vector<std::uint32_t> IDs;
    IDs.reserve(m_tileCount);

    std::stringstream dataStream(data);
    std::uint32_t i;
    while (dataStream >> i)
    {
        IDs.push_back(i);
        //TODO this shouldn't assume the first character
        //is a valid value, and it should ignore anything non-numeric.
        if (dataStream.peek() == ',')
        {
            dataStream.ignore();
        }
    }

    createTiles(IDs);
}

void TileLayer::parseUnencoded(const pugi::xml_node& node)
{
    is_infinite = false;
    std::string attribName;
    std::vector<std::uint32_t> IDs;
    IDs.reserve(m_tileCount);

    for (const auto& child : node.children())
    {
        attribName = child.name();
        if (attribName == "tile")
        {
            IDs.push_back(child.attribute("gid").as_uint());
        }
    }

    createTiles(IDs);
}

void tmx::TileLayer::parseCVS_infinite(const pugi::xml_node& node)
{
    is_infinite = true;
    Logger::log("Searching for chunks : \n");
    for (auto child : node.children())
    {
        std::string attribName = child.name();
        if (attribName == "chunk")
        {
            Logger::log ("Handle Chunk : \n");
            m_chunks.push_back({});
            Chunk& cur_chunk = m_chunks[m_chunks.size() - 1];
            cur_chunk.x = child.attribute("x").as_int();
            cur_chunk.y = child.attribute("y").as_int();
            cur_chunk.width = child.attribute("width").as_uint();
            cur_chunk.height = child.attribute("height").as_uint();
            Logger::log("x : " + std::to_string(cur_chunk.x) + " y: " + std::to_string(cur_chunk.y) + " w : " + std::to_string(cur_chunk.width) + " h: " + std::to_string(cur_chunk.height) + '\n'); ;
            std::string data = child.text().as_string();
         

            std::stringstream dataStream(data);
            std::uint32_t i;
            
            while (dataStream >> i)
            {
                //TODO SAME AS THE OTHER FROM TOP
                static const std::uint32_t mask = 0xf0000000;

                cur_chunk.tile_data.push_back({ i & ~mask ,  ((i & mask) >> 28) });

                if (dataStream.peek() == ',')
                {
                    dataStream.ignore();
                }
            }

            Logger::log("Processed chunks : \n");
            for (int i = 0; i < cur_chunk.height; i++)
            {
                std::string line = "";
                for (int j = 0; j < cur_chunk.width; j++)
                {
                   
                   line += std::to_string(cur_chunk.tile_data[i * cur_chunk.height + j].ID) + ' ';
                    
                }
                Logger::log(line);
            }
            


        }
    }
}

void tmx::TileLayer::parseBase64_infinite(const pugi::xml_node& node)
{
    is_infinite = true;
    Logger::log("Searching for chunks : \n");
    for (auto child : node.children())
    {
        std::string attribName = child.name();
        if (attribName == "chunk")
        {
            Logger::log("Handle Chunk : \n");
            m_chunks.push_back({});
            Chunk& cur_chunk = m_chunks[m_chunks.size() - 1];
            cur_chunk.x = child.attribute("x").as_int();
            cur_chunk.y = child.attribute("y").as_int();
            cur_chunk.width = child.attribute("width").as_uint();
            cur_chunk.height = child.attribute("height").as_uint();
            Logger::log("x : " + std::to_string(cur_chunk.x) + " y: " + std::to_string(cur_chunk.y) + " w : " + std::to_string(cur_chunk.width) + " h: " + std::to_string(cur_chunk.height) + '\n'); ;
            
            std::string data = child.text().as_string();
       
            if (data.empty())
            {
                Logger::log("Chunk " + getName() + " has no layer data. Chunk skipped.", Logger::Type::Error);
                return;
            }

            //using a string stream we can remove whitespace such as tabs
            std::stringstream ss;
            ss << data;
            ss >> data;
            data = base64_decode(data);

            std::stringstream dataStream(data);
            
            size_t expectedSize = static_cast<size_t>(cur_chunk.width) * static_cast<size_t>(cur_chunk.height) * 4;
            std::vector<unsigned char> byteData;
            byteData.reserve(expectedSize);

            if (node.attribute("compression"))
            {
                std::size_t dataSize = data.length() * sizeof(unsigned char);
                if (!decompress(data.c_str(), byteData, dataSize, expectedSize))
                {
                    LOG("Failed to decompress layer data, node skipped.", Logger::Type::Error);
                    return;
                }
            }
            else
            {
                
                byteData.insert(byteData.end(), data.begin(), data.end());
            
            }

            static const std::uint32_t mask = 0xf0000000;
            for (auto i = 0u; i < expectedSize - 3u; i += 4u)
            {
                std::uint32_t id = byteData[i] | byteData[i + 1] << 8 | byteData[i + 2] << 16 | byteData[i + 3] << 24;
                cur_chunk.tile_data.push_back({ id & ~mask ,  ((id & mask) >> 28) });

            }

            Logger::log("Processed chunks : \n");
            for (int i = 0; i < cur_chunk.height; i++)
            {
                std::string line = "";
                for (int j = 0; j < cur_chunk.width; j++)
                {

                    line += std::to_string(cur_chunk.tile_data[i * cur_chunk.height + j].ID) + ' ';

                }
                Logger::log(line);
            }
            
        }

    }
}

void TileLayer::createTiles(const std::vector<std::uint32_t>& IDs)
{
    //LOG(IDs.size() != m_tileCount, "Layer tile count does not match expected size. Found: "
    //    + std::to_string(IDs.size()) + ", expected: " + std::to_string(m_tileCount));
    
    static const std::uint32_t mask = 0xf0000000;
    for (const auto& id : IDs)
    {
        m_tiles.emplace_back();
        m_tiles.back().flipFlags = ((id & mask) >> 28);
        m_tiles.back().ID = id & ~mask;
    }
}


