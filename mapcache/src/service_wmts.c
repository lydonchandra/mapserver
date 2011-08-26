/*
 *  Copyright 2010 Thomas Bonfort
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "geocache.h"
#include <apr_strings.h>
#include <math.h>

/** \addtogroup services */
/** @{ */


static const char *wmts_0 = 
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<Capabilities xmlns=\"http://www.opengis.net/wmts/1.0\"\n"
      "xmlns:ows=\"http://www.opengis.net/ows/1.1\"\n"
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\"\n"
      "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
      "xmlns:gml=\"http://www.opengis.net/gml\" xsi:schemaLocation=\"http://www.opengis.net/wmts/1.0 http://schemas.opengis.net/wmts/1.0/wmtsGetCapabilities_response.xsd\"\n"
      "version=\"1.0.0\">\n"
      "<ows:ServiceIdentification>\n"
      "  <ows:Title>%s</ows:Title>\n"
      "  <ows:ServiceType>OGC WMTS</ows:ServiceType>\n"
      "  <ows:ServiceTypeVersion>1.0.0</ows:ServiceTypeVersion>\n"
      "</ows:ServiceIdentification>\n"
      "<ows:ServiceProvider>\n"
      "  <ows:ProviderName>%s</ows:ProviderName>\n"
      "  <ows:ProviderSite xlink:href=\"%s/wmts?\" />\n"
      "  <ows:ServiceContact>\n"
      "    <ows:IndividualName>%s</ows:IndividualName>\n"
      "  </ows:ServiceContact>\n"
      "</ows:ServiceProvider>\n"
      "<ows:OperationsMetadata>\n"
      "  <ows:Operation name=\"GetCapabilities\">\n"
      "    <ows:DCP>\n"
      "      <ows:HTTP>\n"
      "        <ows:Get xlink:href=\"%s/wmts?\">\n"
      "          <ows:Constraint name=\"GetEncoding\">\n"
      "            <ows:AllowedValues>\n"
      "              <ows:Value>KVP</ows:Value>\n"
      "            </ows:AllowedValues>\n"
      "          </ows:Constraint>\n"
      "        </ows:Get>\n"
      "      </ows:HTTP>\n"
      "    </ows:DCP>\n"
      "  </ows:Operation>\n"
      "  <ows:Operation name=\"GetTile\">\n"
      "    <ows:DCP>\n"
      "      <ows:HTTP>\n"
      "        <ows:Get xlink:href=\"%s/wmts?\">\n"
      "          <ows:Constraint name=\"GetEncoding\">\n"
      "            <ows:AllowedValues>\n"
      "              <ows:Value>KVP</ows:Value>\n"
      "            </ows:AllowedValues>\n"
      "          </ows:Constraint>\n"
      "        </ows:Get>\n"
      "      </ows:HTTP>\n"
      "    </ows:DCP>\n"
      "  </ows:Operation>\n"
      "</ows:OperationsMetadata>\n"
      "<Contents>\n";
      

static const char *wmts_matrix = 
      "    <TileMatrix>\n"
      "      <ows:Identifier>%s:%d</ows:Identifier>\n"
      "      <ScaleDenominator>%.20f</ScaleDenominator>\n"
      "      <TopLeftCorner>%f %f</TopLeftCorner>\n"
      "      <TileWidth>%d</TileWidth>\n"
      "      <TileHeight>%d</TileHeight>\n"
      "      <MatrixWidth>%d</MatrixWidth>\n"
      "      <MatrixHeight>%d</MatrixHeight>\n"
      "    </TileMatrix>\n";

void _create_capabilities_wmts(geocache_context *ctx, geocache_request_get_capabilities *req, char *url, char *path_info, geocache_cfg *cfg) {
   geocache_request_get_capabilities_wmts *request = (geocache_request_get_capabilities_wmts*)req;
   char *caps;
#ifdef DEBUG
   if(request->request.request.type != GEOCACHE_REQUEST_GET_CAPABILITIES) {
      ctx->set_error(ctx,GEOCACHE_ERROR,"wrong wms capabilities request");
      return;
   }
#endif
   
   const char *title;
   const char *onlineresource = apr_table_get(cfg->metadata,"url");
   if(!onlineresource) {
      onlineresource = url;
   }
   
   title = apr_table_get(cfg->metadata,"title");
   if(!title) {
      title = "no title set, add some in metadata";
   }
   
   request->request.mime_type = apr_pstrdup(ctx->pool,"application/xml");
   
   caps = apr_psprintf(ctx->pool,wmts_0,
         title, "providername_todo", onlineresource, "individualname_todo",
         onlineresource,onlineresource,onlineresource);
   
   
   apr_hash_index_t *grid_index = apr_hash_first(ctx->pool,cfg->grids);

   while(grid_index) {
      geocache_grid *grid;
      char *epsgnum;
      const void *key; apr_ssize_t keylen;
      char *matrix;
      int level;
      const char *WellKnownScaleSet;
      apr_hash_this(grid_index,&key,&keylen,(void**)&grid);
      
      /*locate the number after epsg: in the grd srs*/
      epsgnum = strchr(grid->srs,':');
      if(!epsgnum) {
         epsgnum = grid->srs;
      } else {
         epsgnum++;
      }
      
      WellKnownScaleSet = apr_table_get(grid->metadata,"WellKnownScaleSet");
      
      caps = apr_psprintf(ctx->pool,"%s"
            "  <TileMatrixSet>\n"
            "    <ows:Identifier>%s</ows:Identifier>\n"
            "    <ows:SupportedCRS>urn:ogc:def:crs:EPSG::%s</ows:SupportedCRS>\n",
            caps,grid->name,epsgnum);
      
      if(WellKnownScaleSet) {
         caps = apr_psprintf(ctx->pool,"%s"
            "    <WellKnownScaleSet>%s</WellKnownScaleSet>\n",
            caps,WellKnownScaleSet);
      }
      
      for(level=0;level<grid->levels;level++) {
         int matrixwidth, matrixheight;
         double scaledenom, unitwidth, unitheight;
         unitwidth = grid->tile_sx * grid->resolutions[level];
         unitheight = grid->tile_sy * grid->resolutions[level];
                  
         scaledenom = grid->resolutions[level] * geocache_meters_per_unit[grid->unit] / 0.00028;
         matrixwidth = ceil((grid->extents[level][2]-grid->extents[level][0] - 0.01 * unitwidth)/unitwidth);
         matrixheight = ceil((grid->extents[level][3]-grid->extents[level][1] - 0.01* unitheight)/unitheight);      
         matrix = apr_psprintf(ctx->pool,wmts_matrix,
               grid->name, level,
               scaledenom,
               grid->extents[level][0],grid->extents[level][3],
               grid->tile_sx, grid->tile_sy,
               matrixwidth,matrixheight);
         caps = apr_psprintf(ctx->pool,"%s%s",caps,matrix);
      }
      caps = apr_pstrcat(ctx->pool,caps,"  </TileMatrixSet>\n",NULL);
      grid_index = apr_hash_next(grid_index);
   }
   
   apr_hash_index_t *layer_index = apr_hash_first(ctx->pool,cfg->tilesets);
   while(layer_index) {
      geocache_tileset *tileset;
      const void *key; apr_ssize_t keylen;
      apr_hash_this(layer_index,&key,&keylen,(void**)&tileset);
      
      const char *title = apr_table_get(tileset->metadata,"title");
      if(!title) {
         title = "no title set, add some in metadata";
      }
      const char *abstract = apr_table_get(tileset->metadata,"abstract");
      if(!abstract) {
         abstract = "no abstract set, add some in metadata";
      }
      
      caps = apr_psprintf(ctx->pool,"%s"
            "  <Layer>\n"
            "    <ows:Title>%s</ows:Title>\n"
            "    <ows:Abstract>%s</ows:Abstract>\n"
            /*"    <ows:WGS84BoundingBox>\n"
            "      <ows:LowerCorner>%f %f</ows:LowerCorner>\n"
            "      <ows:UpperCorner>%f %f</ows:UpperCorner>\n"
            "    </ows:WGS84BoundingBox>\n"*/
            "    <ows:Identifier>%s</ows:Identifier>\n"
            "    <Style isDefault=\"true\">\n"
            "      <ows:Identifier>_null</ows:Identifier>\n"
            "    </Style>\n"
            "    <Format>%s</Format>\n"
            "    <TileMatrixSetLink>\n"
            "      <TileMatrixSet>%s</TileMatrixSet>\n"
            "    </TileMatrixSetLink>\n"
            "  </Layer>",caps,title,abstract,
            tileset->name,tileset->format->mime_type,tileset->grid->name);
      layer_index = apr_hash_next(layer_index);
   }
   caps = apr_pstrcat(ctx->pool,caps,"</Contents>\n</Capabilities>\n",NULL);
   request->request.capabilities = caps;
}

/**
 * \brief parse a WMTS request
 * \private \memberof geocache_service_wmts
 * \sa geocache_service::parse_request()
 */
void _geocache_service_wmts_parse_request(geocache_context *ctx, geocache_request **request,
      const char *pathinfo, apr_table_t *params, geocache_cfg *config) {
   const char *str, *layer, *matrixset, *matrix;
   geocache_tileset *tileset;
   int row,col,level;
   str = apr_table_get(params,"SERVICE");
   if(!str)
      str = apr_table_get(params,"service");
   if(!str) {
      ctx->set_error(ctx,GEOCACHE_REQUEST_ERROR,"received wmts request with no service param");
      return;
   }
   if( strcasecmp(str,"wmts") ) {
      ctx->set_error(ctx,GEOCACHE_REQUEST_ERROR,"received wmts request with invalid service param %s", str);
      return;
   }
      
   str = apr_table_get(params,"REQUEST");
   if(!str)
      str = apr_table_get(params,"request");
   if(!str) {
      ctx->set_error(ctx, GEOCACHE_REQUEST_ERROR, "received wmts request with no request");
      return;
   }
   if( ! strcasecmp(str,"getcapabilities") ) {
      geocache_request_get_capabilities_wmts *req = (geocache_request_get_capabilities_wmts*)
            apr_pcalloc(ctx->pool,sizeof(geocache_request_get_capabilities_wmts));
      req->request.request.type = GEOCACHE_REQUEST_GET_CAPABILITIES;
      *request = (geocache_request*)req;
      return;
   } else if( strcasecmp(str,"gettile")) {
      ctx->set_error(ctx, GEOCACHE_REQUEST_ERROR, "received wmts request with invalid request %s",str);
      return;
   } else {
      
      str = apr_table_get(params,"TILEROW");
      if(!str)
         str = apr_table_get(params,"tilerow");
      if(!str) {
         ctx->set_error(ctx, GEOCACHE_REQUEST_ERROR, "received wmts request with no TILEROW");
         return;
      } else {
         char *endptr;
         row = (int)strtol(str,&endptr,10);
         if(*endptr != 0 || row < 0) {
            ctx->set_error(ctx, GEOCACHE_REQUEST_ERROR, "received wms request with invalid TILEROW");
            return;
         }
      }
      
      str = apr_table_get(params,"TILECOL");
      if(!str)
         str = apr_table_get(params,"tilecol");
      if(!str) {
         ctx->set_error(ctx, GEOCACHE_REQUEST_ERROR, "received wmts request with no TILECOL");
         return;
      } else {
         char *endptr;
         col = (int)strtol(str,&endptr,10);
         if(*endptr != 0 || col < 0) {
            ctx->set_error(ctx, GEOCACHE_REQUEST_ERROR, "received wms request with invalid TILECOL");
            return;
         }
      }
      
      
      
      layer = apr_table_get(params,"LAYER");
      if(!layer)
         layer = apr_table_get(params,"layer");
      if(!layer) {
         ctx->set_error(ctx, GEOCACHE_REQUEST_ERROR, "received wmts request with no layer");
         return;
      } else {
         tileset = geocache_configuration_get_tileset(config,layer);
         if(!tileset) {
            ctx->set_error(ctx, GEOCACHE_REQUEST_ERROR, "received wmts request with invalid layer %s",layer);
            return;
         }
      }
      
      matrixset = apr_table_get(params,"TILEMATRIXSET");
      if(!matrixset)
         matrixset = apr_table_get(params,"tilematrixset");
      if(!matrixset) {
         ctx->set_error(ctx, GEOCACHE_REQUEST_ERROR, "received wmts request with no TILEMATRIXSET");
         return;
      } else {
         if(strcmp(tileset->grid->name,matrixset)) {
            ctx->set_error(ctx, GEOCACHE_REQUEST_ERROR, "received wmts request with invalid TILEMATRIXSET %s",matrixset);
            return;
         }
      }

      matrix = apr_table_get(params,"TILEMATRIX");
      if(!matrix)
         matrix = apr_table_get(params,"tilematrix");
      if(!matrix) {
         ctx->set_error(ctx, GEOCACHE_REQUEST_ERROR, "received wmts request with no TILEMATRIX");
         return;
      } else {
         const char *levelptr=NULL,*key; /*ptr to the last part of tilematrix:level*/
         for(key=matrix;*key;key++) if(*key == ':') levelptr=key;
         if(!levelptr || !*(++levelptr)) {
            ctx->set_error(ctx, GEOCACHE_REQUEST_ERROR, "received wmts request with invalid TILEMATRIX %s", matrix);
            return;
         } else {
            char *endptr;
            level = (int)strtol(levelptr,&endptr,10);
            if(*endptr != 0 || level < 0 || level >= tileset->grid->levels) {
               ctx->set_error(ctx, GEOCACHE_REQUEST_ERROR, "received wms request with invalid TILEMATRIX %s", matrix);
               return;
            }
         }
      }
      
      geocache_request_get_tile *req = (geocache_request_get_tile*)apr_pcalloc(
                  ctx->pool,sizeof(geocache_request_get_tile));
      req->request.type = GEOCACHE_REQUEST_GET_TILE;
      req->ntiles = 1;
      req->tiles = (geocache_tile**)apr_pcalloc(ctx->pool,sizeof(geocache_tile*));     
      
      
      req->tiles[0] = geocache_tileset_tile_create(ctx->pool, tileset);
      if(!req->tiles[0]) {
         ctx->set_error(ctx, GEOCACHE_ALLOC_ERROR, "failed to allocate tile");
         return;
      }
      
      double unitheight = tileset->grid->tile_sy * tileset->grid->resolutions[level];
      double unitwidth = tileset->grid->tile_sx * tileset->grid->resolutions[level];
            
      int matrixheight = ceil((tileset->grid->extents[level][3]-tileset->grid->extents[level][1] - 0.01* unitheight)/unitheight);
      int matrixwidth = ceil((tileset->grid->extents[level][2]-tileset->grid->extents[level][0] - 0.01* unitwidth)/unitwidth);
      
      if(row >= matrixheight) {
         ctx->set_error(ctx, GEOCACHE_ALLOC_ERROR, "tilerow %d to large for selected tilematrix (max is %d)",row,matrixheight);
         return;
      }

      if(col >= matrixwidth) {
         ctx->set_error(ctx, GEOCACHE_ALLOC_ERROR, "tilecol %d to large for selected tilematrix (max is %d)",col,matrixwidth);
         return;
      }
      
      row = matrixheight-row-1;
      
      
      
      req->tiles[0]->x = col;
      req->tiles[0]->y = row;
      req->tiles[0]->z = level;
      
      *request = (geocache_request*)req;
      return;
   }
}

geocache_service* geocache_service_wmts_create(geocache_context *ctx) {
   geocache_service_wmts* service = (geocache_service_wmts*)apr_pcalloc(ctx->pool, sizeof(geocache_service_wmts));
   if(!service) {
      ctx->set_error(ctx, GEOCACHE_ALLOC_ERROR, "failed to allocate wtms service");
      return NULL;
   }
   service->service.url_prefix = apr_pstrdup(ctx->pool,"wmts");
   service->service.type = GEOCACHE_SERVICE_WMTS;
   service->service.parse_request = _geocache_service_wmts_parse_request;
   service->service.create_capabilities_response = _create_capabilities_wmts;
   return (geocache_service*)service;
}

/** @} */