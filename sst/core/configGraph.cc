// Copyright 2009-2010 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2009-2010, Sandia Corporation
// All rights reserved.
// 
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.


#include <sst_config.h>
#include "sst/core/serialization/core.h"

#include <boost/mpi.hpp>
#include <boost/format.hpp>

#include <sst/core/simulation.h>
#include <sst/core/configGraph.h>
#include <sst/core/debug.h>
#include <sst/core/timeLord.h>

namespace mpi = boost::mpi;

using namespace std;

namespace SST {

#define _GRAPH_DBG( fmt, args...) __DBG( DBG_GRAPH, Graph, fmt, ## args )

int Vertex::count = 0;
int Edge::count = 0;

ComponentId_t ConfigComponent::count = 0;
LinkId_t ConfigLink::count = 0;

static inline int min( int x, int y ) { return x < y ? x : y; }

void ConfigComponent::print_component(std::ostream &os) const {
    os << "Component " << name << " (id = " << id << ")" << std::endl;
    os << "  type = " << type << std::endl;
    os << "  weight = " << weight << std::endl;
    os << "  rank = " << rank << std::endl;
    os << "  isIntrospector = " << isIntrospector << std::endl;
    os << "  Links:" << std::endl;
    for (size_t i = 0 ; i != links.size() ; ++i) {
	links[i]->print_link(os);
    }
    
    os << "  Params:" << std::endl;
    params.print_all_params(os);
    
}

void
ConfigGraph::setComponentRanks(int rank)
{
    for ( ConfigComponentMap_t::iterator iter = comps.begin();
                            iter != comps.end(); ++iter )
    {
	(*iter).second->rank = rank;
    }
    
}


ComponentId_t
ConfigGraph::addComponent(string name, string type, float weight, int rank)
{
    ConfigComponent* comp = new ConfigComponent();
    comp->name = name;
    comp->type = type;
    comp->weight = weight;
    comp->rank = rank;

    comps[comp->id] = comp;
    return comp->id;
}

ComponentId_t
ConfigGraph::addComponent(string name, string type)
{
    ConfigComponent* comp = new ConfigComponent();
    comp->name = name;
    comp->type = type;

    comps[comp->id] = comp;
    return comp->id;
}

void
ConfigGraph::setComponentRank(ComponentId_t comp_id, int rank)
{
    if ( comps.find(comp_id) == comps.end() ) {
	cout << "Invalid component id: " << comp_id << " in call to ConfigGraph::setComponentRank, aborting..." << endl;
	abort();
    }
    comps[comp_id]->rank = rank;
}
    
void
ConfigGraph::setComponentWeight(ComponentId_t comp_id, float weight)
{
    if ( comps.find(comp_id) == comps.end() ) {
	cout << "Invalid component id: " << comp_id << " in call to ConfigGraph::setComponentRank, aborting..." << endl;
	abort();
    }
    comps[comp_id]->weight = weight;
}

void
ConfigGraph::addParams(ComponentId_t comp_id, Params& p)
{
    if ( comps.find(comp_id) == comps.end() ) {
	cout << "Invalid component id: " << comp_id << " in call to ConfigGraph::addParams, aborting..." << endl;
	abort();
    }

    comps[comp_id]->params.insert(p.begin(),p.end());
}
    
void
ConfigGraph::addParameter(ComponentId_t comp_id, const string key, const string value, bool overwrite)
{
    if ( comps.find(comp_id) == comps.end() ) {
	cout << "Invalid component id: " << comp_id << " in call to ConfigGraph::addParameter, aborting..." << endl;
	abort();
    }

    if ( overwrite ) {
	comps[comp_id]->params[key] = value;
    }
    else {
	comps[comp_id]->params.insert(pair<string,string>(key,value));
    }
}

void
ConfigGraph::addLink(ComponentId_t comp_id, string link_name, string port, string latency_str)
{
    if ( comps.find(comp_id) == comps.end() ) {
	cout << "Invalid component id: " << comp_id << " in call to ConfigGraph::addLink, aborting..." << endl;
	abort();
    }

    ConfigLink* link;
    if ( links.find(link_name) == links.end() ) {
	link = new ConfigLink();
	link->name = link_name;
	links[link_name] = link;
    }
    else {
	link = links[link_name];
	if ( link->current_ref >= 2 ) {
	    cout << "ERROR: Parsing SDL file: Link " << link_name << " referenced more than two times" << endl;
	    exit(1);
	}	
    }

    // Convert the latency string to a number
    SimTime_t latency = Simulation::getSimulation()->getTimeLord()->getSimCycles(latency_str, "ConfigGraph::addLink");
    
    int index = link->current_ref++;
    link->component[index] = comp_id;
    link->port[index] = port;
    link->latency[index] = latency;

    comps[comp_id]->links.push_back(link);
}
    
ComponentId_t
ConfigGraph::addIntrospector(string name, string type)
{
    ConfigComponent* comp = new ConfigComponent();
    comp->name = name;
    comp->type = type;
    comp->isIntrospector = true;

    comps[comp->id] = comp;
    return comp->id;    
}
    
static int
find_lat(Simulation *sim, SDL_Component *c, std::string edge )
{
    SDL_links_t::iterator iter = c->links.find( edge );
    if ( iter == c->links.end() ) return 0;
    SDL_Link *l = (*iter).second;
    Params::iterator p_iter = l->params.find( "lat" ); 
    if ( p_iter == l->params.end() ) return 0;
    return sim->getTimeLord()->getSimCycles((*p_iter).second, 
                                            "edge " + edge );
}

void
makeGraph(Simulation *sim, SDL_CompMap_t& map, Graph& graph)
{
    std::multimap<std::string,Vertex *> linkMap;
    int vId = 0;

    for ( SDL_CompMap_t::iterator c_iter = map.begin(); 
          c_iter != map.end();
          ++c_iter ) {
        SDL_Component *c = (*c_iter).second;
        Vertex *v = new Vertex( );
        graph.vlist[ v->id() ] = v;

        // this is used as the key to the compMap 
        v->prop_list.set(GRAPH_COMP_NAME, (*c_iter).first );
        v->prop_list.set(GRAPH_ID, boost::str(boost::format("%1%") % vId++ ) );
        v->prop_list.set(GRAPH_WEIGHT, 
                         boost::str(boost::format("%1%") % c->weight  ) );
	v->rank = c->rank;

        for (SDL_links_t::iterator l_iter = c->links.begin(); 
             l_iter != c->links.end();
             ++l_iter ) {
            linkMap.insert( std::pair<std::string,Vertex*>( (*l_iter).first, v ) );
        }
    }

    if ( linkMap.size() % 2 ) { 
        printf("error, linkMap.size() not even\n");
        return;
    }

    for ( std::map<std::string,Vertex*>::iterator iter = linkMap.begin(); 
          iter != linkMap.end(); 
          ++iter ) {
        Vertex *v[2];
        v[0] = (*iter).second;
        ++iter;
        v[1] = (*iter).second;
        std::string edge = (*iter).first;

        Edge *e = new Edge( v[0]->id(), v[1]->id() );
        graph.elist[ e->id() ] = e;

        e->prop_list.set(GRAPH_LINK_NAME, edge );

        int lat = min( find_lat(sim, 
                                map[ v[0]->prop_list.get(GRAPH_COMP_NAME) ], 
                                edge),
                       find_lat(sim, 
                                map[ v[1]->prop_list.get(GRAPH_COMP_NAME) ], 
                                edge) ); 

        e->prop_list.set(GRAPH_WEIGHT, boost::str(boost::format("%1%") % lat) );
        v[0]->adj_list.push_back(e->id());
        v[1]->adj_list.push_back(e->id());
    }
}


void
printGraph(Graph &graph, SDL_CompMap_t &compMap)
{
    mpi::communicator world;
    std::cout << "Rank:" << world.rank() << std::endl;
    std::cout << " Num Vertices=" << graph.num_vertices() << std::endl;

    std::cout << " Edges:" << std::endl;
    for( VertexList_t::iterator iter = graph.vlist.begin(); 
         iter != graph.vlist.end(); ++iter ) {
        Vertex *v = (*iter).second; 
        SDL_Component *sdl_comp = compMap[v->prop_list.get(GRAPH_COMP_NAME)];

//         printf(" %2d type:%6s rank:%s\n", v->id(), 
//                sdl_comp->type().c_str(), v->prop_list.get(GRAPH_RANK).c_str());
        printf(" %2d type:%6s rank:%d\n", v->id(), 
               sdl_comp->type().c_str(), v->rank);
    }

    for( EdgeList_t::iterator iter = graph.elist.begin();
         iter != graph.elist.end(); ++iter ) {
        Edge *e = (*iter).second; 

        std::cout << "  " << e->v(0) << "--" << e->v(1) 
                  << " latency: " << e->prop_list.get(GRAPH_WEIGHT) 
                  << " name: " << e->prop_list.get(GRAPH_LINK_NAME) << std::endl;
    }
}


// find the minimum partition latency which passes between ranks. This
// gives us the dt for the conservative distance based optimization.
int
findMinPart(Graph &graph)
{
    mpi::communicator world;
    int minPart = 99999;
    if (world.rank() == 0) { // only rank 0 does the search
        for( EdgeList_t::iterator iter = graph.elist.begin(); 
             iter != graph.elist.end(); 
             ++iter ) {
            Edge *e = (*iter).second; 
            int lat = atoi( e->prop_list.get(GRAPH_WEIGHT).c_str() );
//             int sRank = atoi(graph.vlist[e->v(0)]->prop_list.get(GRAPH_RANK).c_str());
//             int tRank = atoi(graph.vlist[e->v(1)]->prop_list.get(GRAPH_RANK).c_str());
            int sRank = graph.vlist[e->v(0)]->rank;
            int tRank = graph.vlist[e->v(1)]->rank;
            if ( sRank != tRank ) {
                if (lat < minPart) minPart = lat;
            }	
	}
    }
    // share the results
    broadcast(world, minPart, 0);

    return minPart;
}

// find the minimum partition latency which passes between ranks. This
// gives us the dt for the conservative distance based optimization.
// int
// findMinPart(ConfigGraph &graph)
// {
//     mpi::communicator world;
//     int minPart = 9999999;
//     if (world.rank() == 0) { // only rank 0 does the search
// 	for( ConfigLinkMap_t::iterator iter = graph.links.begin();
// 	     iter != graph.links.end(); ++iter )
// 	{
// 	    ConfigLink clink* = (*iter).second;
	    
// 	}
//         for( EdgeList_t::iterator iter = graph.elist.begin(); 
//              iter != graph.elist.end(); 
//              ++iter ) {
//             Edge *e = (*iter).second; 
//             int lat = atoi( e->prop_list.get(GRAPH_WEIGHT).c_str() );
//             int sRank = graph.vlist[e->v(0)]->rank;
//             int tRank = graph.vlist[e->v(1)]->rank;
//             if ( sRank != tRank ) {
//                 if (lat < minPart) minPart = lat;
//             }	
// 	}
//     }
//     // share the results
//     broadcast(world, minPart, 0);

//     return minPart;
// }

} // namespace SST
