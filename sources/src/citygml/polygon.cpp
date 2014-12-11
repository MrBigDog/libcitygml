#include "citygml/polygon.h"
#include "citygml/appearance.h"
#include "citygml/texture.h"
#include "citygml/texturecoordinates.h"
#include "citygml/tesselator.h"
#include "citygml/citygmllogger.h"
#include "citygml/texturetargetdefinition.h"
#include "citygml/materialtargetdefinition.h"

#include <algorithm>
#include <stdexcept>
#include <assert.h>

namespace citygml {

    Polygon::Polygon(const std::string& id, std::shared_ptr<CityGMLLogger> logger)  : AppearanceTarget( id ), m_negNormal( false )
    {
        m_logger = logger;
        m_finished = false;
    }

    const std::vector<TVec3d>&Polygon::getVertices() const
    {
        return m_vertices;
    }

    std::vector<TVec3d>& Polygon::getVertices()
    {
        return m_vertices;
    }

    const std::vector<unsigned int>&Polygon::getIndices() const
    {
        return m_indices;
    }


    const Material* Polygon::getMaterialFor(const std::string& theme, bool front) const
    {
        const MaterialTargetDefinition* targetDef = getMaterialTargetDefinitionForTheme(theme, front);
        if (targetDef == nullptr) {
            return nullptr;
        }
        return targetDef->getAppearance().get();
    }

    const Material* Polygon::getMaterialFor(const std::string& theme) const
    {
        const Material* result = getMaterialFor(theme, true);
        if (result != nullptr) {
            return result;
        }
        return getMaterialFor(theme, false);
    }

    const Texture* Polygon::getTextureFor(const std::string& theme, bool front) const
    {
        const TextureTargetDefinition* targetDef = getTextureTargetDefinitionForTheme(theme, front);
        if (targetDef == nullptr) {
            return nullptr;
        }
        return targetDef->getAppearance().get();
    }

    const Texture* Polygon::getTextureFor(const std::string& theme) const
    {
        const Texture* result = getTextureFor(theme, true);
        if (result != nullptr) {
            return result;
        }
        return getTextureFor(theme, false);
    }

    const std::vector<TVec2f> Polygon::getTexCoordsForTheme(const std::string& theme, bool front) const
    {
        // Lazy generation of texCoords
        std::vector<TVec2f> texCoords;

        const TextureTargetDefinition* targetDef = getTextureTargetDefinitionForTheme(theme, front);
        if (targetDef == nullptr) {
            return texCoords;
        }

        std::vector<std::string> orderedRingIDs;

        if ( m_exteriorRing != nullptr )
        {
            orderedRingIDs.push_back(m_exteriorRing->getId());
        }

        for ( const auto& ring : m_interiorRings )
        {
            orderedRingIDs.push_back(ring->getId());
        }

        if (targetDef->getTextureCoordinatesCount() != orderedRingIDs.size()) {
            CITYGML_LOG_WARN(m_logger, "Texture with id '" << targetDef->getAppearance()->getId() << "'' targets Polygon with id '" << this->getId() << "'"
                             << " but the number of TextureCoordinates objects (gml::textureCoordinates) is not equal with the number of LinearRing objects (gml:LinearRing)."
                             << " (Ring objects: " << orderedRingIDs.size() << " TextureCoordinates objects: " << targetDef->getTextureCoordinatesCount());
        }


        for (const std::string& ringId : orderedRingIDs) {

            const TextureCoordinates* coordinates = targetDef->getTextureCoordinatesForID(ringId);

            if (!coordinates) {
                CITYGML_LOG_WARN(m_logger, "Texture with id '" << targetDef->getAppearance()->getId() << "'' targets Polygon with id '" << this->getId() << "'"
                                 << " but does not contain a TextureCoordinates object (gml::textureCoordinates) for LinearRing object (gml:LinearRing) with id '" << ringId << "'");
            } else {
                texCoords.insert(texCoords.end(), coordinates->getCoords().begin(), coordinates->getCoords().end());
            }
        }

        // Workaround until vertices removed/added by tesselation are correctly handeld
        if (texCoords.size() > m_vertices.size()) {
            texCoords.resize(m_vertices.size());
        } else if (texCoords.size() < m_vertices.size()) {
            for (size_t i = texCoords.size(); i < m_vertices.size(); i++) {
                texCoords.push_back(texCoords.back());
            }
        }

        return texCoords;
    }

    std::shared_ptr<const Texture> Polygon::getTextureForTheme(const std::string& theme, bool front) const
    {
        const TextureTargetDefinition* targetDef = getTextureTargetDefinitionForTheme(theme, front);

        if (targetDef == nullptr) {
            return nullptr;
        }

        return targetDef->getAppearance();
    }


    TVec3d Polygon::computeNormal()
    {
        if ( m_exteriorRing == nullptr ) return TVec3d();

        TVec3d normal = m_exteriorRing->computeNormal();

        return m_negNormal ? -normal : normal;
    }

    bool Polygon::negNormal() const
    {
        return m_negNormal;
    }

    void Polygon::setNegNormal(bool negNormal)
    {
        m_negNormal = negNormal;
    }

    void Polygon::removeDuplicateVerticesInRings(std::shared_ptr<CityGMLLogger> logger) {
        std::vector<TextureTargetDefinition*> texTargetDefinitions = this->getTextureTargetDefinitions();

        // mergeRings should be done before merging polygons... hence m_exteriorRings should only contain one object
        if ( m_exteriorRing != nullptr )
        {
            m_exteriorRing->removeDuplicateVertices( texTargetDefinitions, logger );
        }

        for ( std::unique_ptr<LinearRing>& ring : m_interiorRings )
        {
            ring->removeDuplicateVertices( texTargetDefinitions, logger );
        }
    }

    void Polygon::createSimpleIndices(std::shared_ptr<CityGMLLogger> logger)
    {
        if ( m_exteriorRing != nullptr )
        {
            m_vertices.insert(m_vertices.end(), m_exteriorRing->getVertices().begin(), m_exteriorRing->getVertices().end());
            m_exteriorRing->forgetVertices();
        }

        for ( std::unique_ptr<LinearRing>& ring : m_interiorRings )
        {
            m_vertices.insert(m_vertices.end(), ring->getVertices().begin(), ring->getVertices().end());
            ring->forgetVertices();
        }

        // Create triangles' indices
        int indicesSize = 3 * ( m_vertices.size() - 2 );
        if ( indicesSize < 3 ) return;
        m_indices.resize( indicesSize );
        for ( int i = 0, p = 0; p < indicesSize - 2; i++, p += 3 )
            for ( unsigned int j = 0; j < 3; j++ )
                m_indices[ p + j ] = i + j;
    }

    void Polygon::createIndicesWithTesselation(Tesselator& tesselator, std::shared_ptr<CityGMLLogger> logger)
    {
        TVec3d normal = computeNormal();

        size_t numVertices = 0;
        if (m_exteriorRing != nullptr) {
            numVertices += m_exteriorRing->getVertices().size();
        }

        for ( std::unique_ptr<LinearRing>& ring : m_interiorRings )
        {
            numVertices += ring->getVertices().size();
        }

        tesselator.init(numVertices, normal);

        if (m_exteriorRing != nullptr) {
            tesselator.addContour( m_exteriorRing->getVertices() );
            m_exteriorRing->forgetVertices();
        }

        for ( std::unique_ptr<LinearRing>& ring : m_interiorRings )
        {
            tesselator.addContour( ring->getVertices() );
            ring->forgetVertices();
        }

        tesselator.addContour( m_vertices );
        tesselator.compute();
        m_vertices = tesselator.getVertices();
        m_indices = tesselator.getIndices();

        if (numVertices != m_vertices.size()) {
            CITYGML_LOG_ERROR(logger, "Tesselation of Polygon with id '" << this->getId() << "' has changed the number of Polygons, "
                               << "causing a mismatch of texture coordinates and vertices.");
        }
    }

    void Polygon::computeIndices(bool tesselate, Tesselator& tesselator, std::shared_ptr<CityGMLLogger> logger )
    {
        m_indices.clear();
        m_vertices.clear();

        if (!tesselate) {
            createSimpleIndices(logger);
        } else {
            createIndicesWithTesselation(tesselator, logger);
        }

        if ( m_vertices.size() < 3 ) {
            CITYGML_LOG_WARN(logger, "Polygon with id " << this->getId() << " has less than 3 vertices.");
        }
    }

    void Polygon::finish(bool doTesselate , Tesselator& tesselator, bool optimize, std::shared_ptr<CityGMLLogger> logger)
    {
        if (m_finished) {
            // This may happen as Polygons can be shared between geometries
            return;
        }

        m_finished = true;

        if (optimize) {
            removeDuplicateVerticesInRings(logger);
        }

        computeIndices(doTesselate, tesselator, logger);

    }

    void Polygon::addRing( LinearRing* ring )
    {
        if (m_finished) {
            throw std::runtime_error("Can't add LinearRing to finished Polygon.");
        }

        if (ring->isExterior() && m_exteriorRing != nullptr) {
            CITYGML_LOG_WARN(m_logger, "Duplicate definition of exterior LinearRing for Polygon with id '" << this->getId() << "'."
                             << " Keeping exterior LinearRing with id '" << m_exteriorRing->getId() << "' and ignore LinearRing with id '" << ring->getId() << "'");
            delete ring;
            return;
        }

        if ( ring->isExterior() ) {
            m_exteriorRing = std::unique_ptr<LinearRing>(ring);
        }
        else {
            m_interiorRings.push_back( std::unique_ptr<LinearRing>(ring) );
        }
    }

    Polygon::~Polygon()
    {
    }

}
