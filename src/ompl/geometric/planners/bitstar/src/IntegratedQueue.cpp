/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2014, University of Toronto
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the University of Toronto nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

/* Authors: Jonathan Gammell */

//Myself:
#include "ompl/geometric/planners/bitstar/IntegratedQueue.h"

//OMPL:
//For exceptions:
#include "ompl/util/Exception.h"

namespace ompl
{
    namespace geometric
    {
        IntegratedQueue::IntegratedQueue(const VertexPtr& startVertex, const VertexPtr& goalVertex, const neighbourhood_func_t& nearSamplesFunc, const neighbourhood_func_t& nearVerticesFunc, const vertex_heuristic_func_t& lowerBoundHeuristicVertex, const vertex_heuristic_func_t& currentHeuristicVertex, const edge_heuristic_func_t& lowerBoundHeuristicEdge, const edge_heuristic_func_t& currentHeuristicEdge, const edge_heuristic_func_t& currentHeuristicEdgeTarget)
            :   opt_(startVertex->getOpt()),
                startVertex_(startVertex),
                goalVertex_(goalVertex),
                nearSamplesFunc_(nearSamplesFunc),
                nearVerticesFunc_(nearVerticesFunc),
                lowerBoundHeuristicVertexFunc_(lowerBoundHeuristicVertex),
                currentHeuristicVertexFunc_(currentHeuristicVertex),
                lowerBoundHeuristicEdgeFunc_(lowerBoundHeuristicEdge),
                currentHeuristicEdgeFunc_(currentHeuristicEdge),
                currentHeuristicEdgeTargetFunc_(currentHeuristicEdgeTarget),
                useFailureTracking_(false),
                outgoingLookupTables_(true),
                incomingLookupTables_(true),
                vertexQueue_( boost::bind(&IntegratedQueue::vertexQueueComparison, this, _1, _2) ), //This tells the vertexQueue_ to use the vertexQueueComparison for sorting
                vertexToExpand_( vertexQueue_.begin() ),
                edgeQueue_( boost::bind(&IntegratedQueue::edgeQueueComparison, this, _1, _2) ), //This tells the edgeQueue_ to use the edgeQueueComparison for sorting
                vertexIterLookup_(),
                outgoingEdges_(),
                incomingEdges_(),
                resortVertices_(),
                costThreshold_( std::numeric_limits<double>::infinity() ) //Purposeful gibberish
        {
            //The cost threshold:
            costThreshold_ = opt_->infiniteCost();
        }



        void IntegratedQueue::insertVertex(const VertexPtr& newVertex)
        {
            //Insert the vertex:
            this->vertexInsertHelper(newVertex, true);
        }



        void IntegratedQueue::insertEdge(const vertex_pair_t& newEdge)
        {
            //Call my helper function:
            this->edgeInsertHelper(newEdge, edgeQueue_.end());
        }



        void IntegratedQueue::eraseVertex(const VertexPtr& oldVertex, bool disconnectParent)
        {
            //If requested, disconnect from parent, cascading cost updates:
            if (disconnectParent == true)
            {
                this->disconnectParent(oldVertex, true);
            }

            //Remove it from vertx queue and lookup, and edge queues (as requested):
            this->vertexRemoveHelper(oldVertex, vertex_nn_ptr_t(), vertex_nn_ptr_t(), true);
        }



        VertexPtr IntegratedQueue::frontVertex()
        {
            if (this->isEmpty() == true)
            {
                throw ompl::Exception("Attempted to access the first element in an empty IntegratedQueue.");
            }

            //Update the queue:
            this->updateQueue();

            //Return the front edge
            return vertexQueue_.begin()->second;
        }



        IntegratedQueue::vertex_pair_t IntegratedQueue::frontEdge()
        {
            if (this->isEmpty() == true)
            {
                throw ompl::Exception("Attempted to access the first element in an empty IntegratedQueue.");
            }

            //Update the queue:
            this->updateQueue();

            //Return the front edge
            return edgeQueue_.begin()->second;
        }



        ompl::base::Cost IntegratedQueue::frontVertexValue()
        {
            if (this->isEmpty() == true)
            {
                throw ompl::Exception("Attempted to access the first element in an empty IntegratedQueue.");
            }

            //Update the queue:
            this->updateQueue();

            //Return the front value
            return vertexQueue_.begin()->first;
        }



        IntegratedQueue::cost_pair_t IntegratedQueue::frontEdgeValue()
        {
            if (this->isEmpty() == true)
            {
                throw ompl::Exception("Attempted to access the first element in an empty IntegratedQueue.");
            }

            //Update the queue:
            this->updateQueue();

            //Return the front value
            return edgeQueue_.begin()->first;
        }



        void IntegratedQueue::popFrontEdge(vertex_pair_t& bestEdge)
        {
            if (this->isEmpty() == true)
            {
                throw ompl::Exception("Attempted to pop an empty IntegratedQueue.");
            }

            //Update the queue:
            this->updateQueue();

            //Return the front:
            bestEdge = edgeQueue_.begin()->second;

            //Erase the edge:
            this->edgeRemoveHelper(edgeQueue_.begin(), true, true);
        }



        IntegratedQueue::vertex_pair_t IntegratedQueue::popFrontEdge()
        {
            vertex_pair_t rval;

            this->popFrontEdge(rval);

            return rval;
        }



        void IntegratedQueue::setThreshold(const ompl::base::Cost& costThreshold)
        {
            costThreshold_ = costThreshold;
        }



        void IntegratedQueue::removeEdgesTo(const VertexPtr& cVertex)
        {
            if (edgeQueue_.empty() == false)
            {
                if (incomingLookupTables_ == true)
                {
                    //Variable:
                    //The iterator to the vector of edges to the child:
                    vid_edge_queue_iter_umap_t::iterator toDeleteIter;

                    //Get the vector of iterators
                    toDeleteIter = incomingEdges_.find(cVertex->getId());

                    //Make sure it was found before we start dereferencing it:
                    if (toDeleteIter != incomingEdges_.end())
                    {
                        //Iterate over the vector removing them from queue
                        for (edge_queue_iter_list_t::iterator listIter = toDeleteIter->second.begin(); listIter != toDeleteIter->second.end(); ++listIter)
                        {
                            //Erase the edge, removing it from the *other* lookup. No need to remove from this lookup, as that's being cleared:
                            this->edgeRemoveHelper(*listIter, false, true);
                        }

                        //Clear the list:
                        toDeleteIter->second = edge_queue_iter_list_t();
                    }
                    //No else, why was this called?
                }
                else
                {
                    throw ompl::Exception("Child lookup is not enabled for this instance of the container.");
                }
            }
            //No else, nothing to remove_to
        }



        void IntegratedQueue::removeEdgesFrom(const VertexPtr& pVertex)
        {
            if (edgeQueue_.empty() == false)
            {
                if (outgoingLookupTables_ == true)
                {
                    //Variable:
                    //The iterator to the vector of edges from the parent:
                    vid_edge_queue_iter_umap_t::iterator toDeleteIter;

                    //Get the vector of iterators
                    toDeleteIter = outgoingEdges_.find(pVertex->getId());

                    //Make sure it was found before we start dereferencing it:
                    if (toDeleteIter != outgoingEdges_.end())
                    {
                        //Iterate over the vector removing them from queue
                        for (edge_queue_iter_list_t::iterator listIter = toDeleteIter->second.begin(); listIter != toDeleteIter->second.end(); ++listIter)
                        {
                            //Erase the edge, removing it from the *other* lookup. No need to remove from this lookup, as that's being cleared:
                            this->edgeRemoveHelper(*listIter, true, false);
                        }

                        //Clear the list:
                        toDeleteIter->second = edge_queue_iter_list_t();
                    }
                    //No else, why was this called?
                }
                else
                {
                    throw ompl::Exception("Removing edges in the queue coming from a vertex requires parent vertex lookup, which is not enabled for this instance of the container.");
                }
            }
            //No else, nothing to remove_from
        }



        void IntegratedQueue::pruneEdgesTo(const VertexPtr& cVertex)
        {
            if (edgeQueue_.empty() == false)
            {
                if (incomingLookupTables_ == true)
                {
                    //Variable:
                    //The iterator to the key,value of the child-lookup map, i.e., an iterator to a pair whose second is a list of edges to the child (which are actually iterators to the queue):
                    vid_edge_queue_iter_umap_t::iterator itersToVertex;

                    //Get my incoming edges as a vector of iterators
                    itersToVertex = incomingEdges_.find(cVertex->getId());

                    //Make sure it was found before we start dereferencing it:
                    if (itersToVertex != incomingEdges_.end())
                    {
                        //Variable
                        //The vector of edges to delete in the list:
                        std::vector<edge_queue_iter_list_t::iterator> listItersToDelete;

                        //Iterate over the incoming edges and record those that are to be deleted
                        for (edge_queue_iter_list_t::iterator listIter = itersToVertex->second.begin(); listIter != itersToVertex->second.end(); ++listIter)
                        {
                            //Check if it is to be pruned
                            if ( this->edgePruneCondition((*listIter)->second) == true )
                            {
                                listItersToDelete.push_back(listIter);
                            }
                            //No else, we're not deleting this iterator
                        }

                        //Now, iterate over the list of iterators to delete
                        for (unsigned int i = 0u; i < listItersToDelete.size(); ++i)
                        {
                            //Remove the edge and the edge iterator from the other lookup table:
                            this->edgeRemoveHelper( *listItersToDelete.at(i), false, true);

                            //And finally erase the lookup iterator from the from lookup. If this was done first, the iterator would be invalidated for the above.
                            itersToVertex->second.erase( listItersToDelete.at(i) );
                        }
                    }
                    //No else, nothing to delete
                }
                else
                {
                    throw ompl::Exception("Removing edges in the queue going to a vertex requires child vertex lookup, which is not enabled for this instance of the container.");
                }
            }
            //No else, nothing to prune_to
        }



        void IntegratedQueue::pruneEdgesFrom(const VertexPtr& pVertex)
        {
            if (edgeQueue_.empty() == false)
            {
                if (outgoingLookupTables_ == true)
                {
                    //Variable:
                    //The iterator to the key, value of the parent-lookup map, i.e., an iterator to a pair whose second is a list of edges from the child (which are actually iterators to the queue):
                    vid_edge_queue_iter_umap_t::iterator itersFromVertex;

                    //Get my outgoing edges as a vector of iterators
                    itersFromVertex = outgoingEdges_.find(pVertex->getId());

                    //Make sure it was found before we start dereferencing it:
                    if (itersFromVertex != outgoingEdges_.end())
                    {
                        //Variable
                        //The vector of edges to delete in the list:
                        std::vector<edge_queue_iter_list_t::iterator> listItersToDelete;

                        //Iterate over the incoming edges and record those that are to be deleted
                        for (edge_queue_iter_list_t::iterator listIter = itersFromVertex->second.begin(); listIter != itersFromVertex->second.end(); ++listIter)
                        {
                            //Check if it is to be pruned
                            if ( this->edgePruneCondition((*listIter)->second) == true )
                            {
                                listItersToDelete.push_back(listIter);
                            }
                            //No else, we're not deleting this iterator
                        }

                        //Now, iterate over the list of iterators to delete
                        for (unsigned int i = 0u; i < listItersToDelete.size(); ++i)
                        {
                            //Remove the edge and the edge iterator from the other lookup table:
                            this->edgeRemoveHelper( *listItersToDelete.at(i), true, false );

                            //And finally erase the lookup iterator from the from lookup. If this was done first, the iterator would be invalidated for the above.
                            itersFromVertex->second.erase( listItersToDelete.at(i) );

                        }
                    }
                    //No else, nothing to delete
                }
                else
                {
                    throw ompl::Exception("Parent lookup is not enabled for this instance of the container.");
                }
            }
            //No else, nothing to prune_from
        }



        void IntegratedQueue::markVertexUnsorted(const VertexPtr& vertex)
        {
            resortVertices_.push_back(vertex);

            //This is the idea for a future assert:
//            if (vertexIterLookup_.find(vertex->getId()) == vertexIterLookup_.end())
//            {
//                throw ompl::Exception("A vertex was marked that was not in the queue...");
//            }
        }



        std::pair<unsigned int, unsigned int> IntegratedQueue::prune(const vertex_nn_ptr_t& vertexNN, const vertex_nn_ptr_t& freeStateNN)
        {
            if (this->isSorted() == false)
            {
                throw ompl::Exception("Prune cannot be called on an unsorted queue.");
            }
            //The vertex expansion queue is sorted on an estimated solution cost considering the *current* cost-to-come of the vertices, while we prune by considering the best-case cost-to-come.
            //This means that the value of the vertices in the queue are an upper-bounding estimate of the value we will use to prune them.
            //Therefore, we can start our pruning at the goal vertex and iterate forward through the queue from there.

            //Variables:
            //The number of vertices and samples pruned:
            std::pair<unsigned int, unsigned int> numPruned;
            //The iterator into the lookup helper:
            vid_vertex_queue_iter_umap_t::iterator lookupIter;
            //The iterator into the queue:
            vertex_queue_iter_t queueIter;

            //Initialize the counters:
            numPruned = std::make_pair(0u, 0u);

            //Get the iterator to the queue to the goal.
            lookupIter = vertexIterLookup_.find(goalVertex_->getId());

            //Check that it was found
            if (lookupIter == vertexIterLookup_.end())
            {
                //Complain
                throw ompl::Exception("The goal vertex is not in the queue?");
            }

            //Get the iterator to the goal vertex in the queue:
            queueIter = lookupIter->second;

            //Move to the one after:
            ++queueIter;

            //Iterate through to the end of the queue
            while (queueIter != vertexQueue_.end())
            {
                //Check if it should be pruned (value) or has lost its parent.
                if (this->vertexPruneCondition(queueIter->second) == true)
                {
                    //The vertex should be pruned.
                    //Variables
                    //An iter to the vertex to prune:
                    vertex_queue_iter_t pruneIter;

                    //Copy the iterator to prune:
                    pruneIter = queueIter;

                    //Move the queue iterator back one so we can step to the next *valid* vertex after pruning:
                    --queueIter;

                    //Prune the branch:
                    numPruned = this->pruneBranch(pruneIter->second, vertexNN, freeStateNN);
                }
                //No else, skip this vertex.

                //Iterate forward to the next value in the queue
                ++queueIter;
            }

            //Return the number of vertices and samples pruned.
            return numPruned;
        }



        std::pair<unsigned int, unsigned int> IntegratedQueue::resort(const vertex_nn_ptr_t& vertexNN, const vertex_nn_ptr_t& freeStateNN)
        {
            //Variable:
            typedef boost::unordered_map<Vertex::id_t, VertexPtr> id_ptr_umap_t;
            typedef std::map<unsigned int, id_ptr_umap_t> depth_id_ptr_map_t;
            //The number of vertices and samples pruned, respectively:
            std::pair<unsigned int, unsigned int> numPruned;

            //Initialize the counters:
            numPruned = std::make_pair(0u, 0u);

            //Iterate through every vertex listed for resorting:
            if (resortVertices_.empty() == false)
            {
                //Variable:
                //The container ordered on vertex depth:
                depth_id_ptr_map_t uniqueResorts;

                //Iterate over the vector and place into the unique queue indexed on *depth*. This guarantees that we won't process a branch multiple times by being given different vertices down its chain
                for (std::list<VertexPtr>::iterator vIter = resortVertices_.begin(); vIter != resortVertices_.end(); ++vIter)
                {
                    //Add the vertex to the unordered map stored at the given depth.
                    //The [] return an reference to the existing entry, or create a new entry:
                    uniqueResorts[(*vIter)->getDepth()].emplace((*vIter)->getId(), *vIter);
                }

                //Clear the list of vertices to resort from:
                resortVertices_.clear();

                //Now process the vertices in order of depth.
                for (depth_id_ptr_map_t::iterator deepIter = uniqueResorts.begin(); deepIter != uniqueResorts.end(); ++deepIter)
                {
                    for (id_ptr_umap_t::iterator vIter = deepIter->second.begin(); vIter != deepIter->second.end(); ++vIter)
                    {
                        //Make sure it has not already been pruned:
                        if (vIter->second->isPruned() == false)
                        {
                            //Make sure it has not already been returned to the set of samples:
                            if (vIter->second->isConnected() == true)
                            {
                                //Are we pruning the vertex from the queue?
                                if (this->vertexPruneCondition(vIter->second) == true)
                                {
                                    //The vertex should just be pruned and forgotten about.
                                    //Prune the branch:
                                    numPruned = this->pruneBranch(vIter->second, vertexNN, freeStateNN);
                                }
                                else
                                {
                                    //The vertex is going to be kept.

                                    //Does it have any children?
                                    if (vIter->second->hasChildren() == true)
                                    {
                                        //Variables:
                                        //The list of children:
                                        std::vector<VertexPtr> resortChildren;

                                        //Put its children in the list to be resorted:
                                        //Get the list of children:
                                        vIter->second->getChildren(&resortChildren);

                                        //Get a reference to the container for the children, all children are 1 level deeper than their parent.:
                                        //The [] return an reference to the existing entry, or create a new entry:
                                        id_ptr_umap_t& depthContainer = uniqueResorts[vIter->second->getDepth() + 1u];

                                        //Place the children into the container, as the container is a map, it will not allow the children to be entered twice.
                                        for (unsigned int i = 0u; i < resortChildren.size(); ++i)
                                        {
                                            depthContainer.emplace(resortChildren.at(i)->getId(), resortChildren.at(i));
                                        }
                                    }

                                    //Reinsert the vertex:
                                    this->reinsertVertex(vIter->second);
                                }
                            }
                            //No else, this vertex was a child of a vertex pruned during the resort. It has been returned to the set of free samples.
                        }
                        //No else, this vertex was a child of a vertex pruned during the resort. It has been deleted.
                    }
                }
            }

            //Return the number of vertices pruned.
            return numPruned;
        }



        void IntegratedQueue::finish()
        {
            //Clear the edge containers:
            edgeQueue_.clear();
            outgoingEdges_.clear();
            incomingEdges_.clear();

            //Do NOT clear:
            //  -  resortVertices_ (they may still need to be resorted)
            //  - vertexIterLookup_ (it's still valid)
        }



        void IntegratedQueue::reset()
        {
            //Make sure the queue is "finished":
            this->finish();

            //Restart the expansion queue:
            vertexToExpand_ = vertexQueue_.begin();
        }



        void IntegratedQueue::clear()
        {
            //Clear:
            //The vertex queue:
            vertexQueue_.clear();
            vertexToExpand_ = vertexQueue_.begin();

            //The edge queue:
            edgeQueue_.clear();

            //The lookups:
            vertexIterLookup_.clear();
            outgoingEdges_.clear();
            incomingEdges_.clear();

            //The resort list:
            resortVertices_.clear();

            //The cost threshold:
            costThreshold_ = opt_->infiniteCost();
        }




        bool IntegratedQueue::vertexPruneCondition(const VertexPtr& state) const
        {
            //Threshold should always be g_t(x_g)
            //As the sample is in the graph (and therefore could be part of g_t), prune iff g^(v) + h^(v) > g_t(x_g)
            //g^(v) + h^(v) <= g_t(x_g)
            return this->isCostWorseThan(lowerBoundHeuristicVertexFunc_(state), costThreshold_);
        }



        bool IntegratedQueue::samplePruneCondition(const VertexPtr& state) const
        {
            //Threshold should always be g_t(x_g)
            //As the sample is not in the graph (and therefore not part of g_t), prune if g^(v) + h^(v) >= g_t(x_g)
            return this->isCostWorseThanOrEquivalentTo(lowerBoundHeuristicVertexFunc_(state), costThreshold_);
        }



        bool IntegratedQueue::edgePruneCondition(const vertex_pair_t& edge) const
        {
            bool rval;
            //Threshold should always be g_t(x_g)

            // g^(v) + c^(v,x) + h^(x) > g_t(x_g)?
            rval = this->isCostWorseThan(lowerBoundHeuristicEdgeFunc_(edge), costThreshold_);


            //If the child is connected already, we need to check if we could do better than it's current connection. But only if we're not pruning based on the first check
            if (edge.second->hasParent() == true && rval == false)
            {
                //g^(v) + c^(v,x) > g_t(x)
                //rval = this->isCostWorseThan(opt_->combineCosts(this->costToComeHeuristic(edge.first), this->edgeCostHeuristic(edge)), edge.second->getCost()); //Ever rewire?
                //g_t(v) + c^(v,x) > g_t(x)
                rval = this->isCostWorseThan(currentHeuristicEdgeTargetFunc_(edge), edge.second->getCost()); //Currently rewire?
            }

            return  rval;
        }



        unsigned int IntegratedQueue::numEdges() const
        {
            return edgeQueue_.size();
        }



        unsigned int IntegratedQueue::numVertices() const
        {
            //Variables:
            //The number of vertices left to expand:
            unsigned int numToExpand;

            //Start at 0:
            numToExpand = 0u;

            //Iterate until the end:
            for (cost_vertex_multimap_t::const_iterator vIter = vertexToExpand_; vIter != vertexQueue_.end(); ++vIter)
            {
                //Increment counter:
                ++numToExpand;
            }

            //Return
            return numToExpand;
        }



        unsigned int IntegratedQueue::numEdgesTo(const VertexPtr& cVertex) const
        {
            //Variables:
            //The number of edges to:
            unsigned int rval;

            //Start at 0:
            rval = 0u;

            //Is there anything to count?
            if (edgeQueue_.empty() == false)
            {
                if (incomingLookupTables_ == true)
                {
                    //Variable:
                    //The iterator to the vector of edges to the child:
                    vid_edge_queue_iter_umap_t::const_iterator toIter;

                    //Get the vector of iterators
                    toIter = incomingEdges_.find(cVertex->getId());

                    //Make sure it was found before we dereferencing it:
                    if (toIter != incomingEdges_.end())
                    {
                        rval = toIter->second.size();
                    }
                    //No else, there are none.
                }
                else
                {
                    throw ompl::Exception("Parent lookup is not enabled for this instance of the container.");
                }
            }
            //No else, there is nothing.

            //Return:
            return rval;
        }



        unsigned int IntegratedQueue::numEdgesFrom(const VertexPtr& pVertex) const
        {
            //Variables:
            //The number of edges to:
            unsigned int rval;

            //Start at 0:
            rval = 0u;

            //Is there anything to count?
            if (edgeQueue_.empty() == false)
            {
                if (outgoingLookupTables_ == true)
                {
                    //Variable:
                    //The iterator to the vector of edges from the parent:
                    vid_edge_queue_iter_umap_t::const_iterator toIter;

                    //Get the vector of iterators
                    toIter = outgoingEdges_.find(pVertex->getId());

                    //Make sure it was found before we dereferencing it:
                    if (toIter != outgoingEdges_.end())
                    {
                        rval = toIter->second.size();
                    }
                    //No else, 0u.
                }
                else
                {
                    throw ompl::Exception("Parent lookup is not enabled for this instance of the container.");
                }
            }
            //No else, there is nothing.

            //Return
            return rval;
        }



        bool IntegratedQueue::isSorted() const
        {
            return resortVertices_.empty();
        }



        bool IntegratedQueue::isEmpty()
        {
            //Expand if the edge queue is empty but the vertex queue is not:
            while (edgeQueue_.empty() && vertexToExpand_ != vertexQueue_.end())
            {
                //Expand the next vertex, this pushes the token:
                this->expandNextVertex();
            }

            //Return whether the edge queue is empty:
            return edgeQueue_.empty();
        }



        void IntegratedQueue::listVertices(std::vector<VertexPtr>* vertexQueue)
        {
            //Clear the given list:
            vertexQueue->clear();

            //Iterate until the end, pushing back:
            for (cost_vertex_multimap_t::const_iterator vIter = vertexToExpand_; vIter != vertexQueue_.end(); ++vIter)
            {
                //Push back:
                vertexQueue->push_back(vIter->second);
            }
        }



        void IntegratedQueue::listEdges(std::vector<vertex_pair_t>* edgeQueue)
        {
            //Clear the vector
            edgeQueue->clear();

            //I don't think there's a std::copy way to do this, so just iterate
            for( cost_pair_vertex_pair_multimap_t::const_iterator eIter = edgeQueue_.begin(); eIter != edgeQueue_.end(); ++eIter )
            {
                edgeQueue->push_back(eIter->second);
            }
        }









        void IntegratedQueue::updateQueue()
        {
            //Variables:
            //Whether to expand:
            bool expand;

            expand = true;
            while ( expand == true )
            {
                //Check if there are any vertices to expand:
                if (vertexToExpand_ != vertexQueue_.end())
                {
                    //Expand a vertex if the edge queue is empty, or the vertex could place a better edge into it:
                    if (edgeQueue_.empty() == true)
                    {
                        //The edge queue is empty, any edge is better than this!
                        this->expandNextVertex();
                    }
                    else if (this->isCostBetterThanOrEquivalentTo( vertexToExpand_->first, edgeQueue_.begin()->first.first ) == true)
                    {
                        //The vertex *could* give a better edge than our current best edge:
                        this->expandNextVertex();
                    }
                    else
                    {
                        //We are done expanding for now:
                        expand = false;
                    }
                }
                else
                {
                    //There are no vertices left to expand
                    expand = false;
                }


//                //Prune back any edges in the front that have previously failed:
//                if (useFailureTracking_ == true)
//                {
//                    if (edgeQueue_.begin()->second.first->hasAlreadyFailed(edgeQueue_.begin()->second.second) == true)
//                    {
//                        //Remove the edge from the queue:
//                        this->edgeRemoveHelper(edgeQueue_.begin(), true, true);
//
//                        //Mark that we may need to expand a new vertex:
//                        expand = true;
//                    }
//                    //No else
//                }
//                //No else
            }
        }



        void IntegratedQueue::expandNextVertex()
        {
            //Should we expand the next vertex? Will it be pruned?
            if (this->vertexPruneCondition(vertexToExpand_->second) == false)
            {
                //Expand the vertex in the front:
                this->expandVertex(vertexToExpand_->second);

                //Increment the vertex token:
                ++vertexToExpand_;
            }
            else
            {
                //The next vertex would get pruned, so just jump to the end:
                vertexToExpand_ = vertexQueue_.end();
            }
        }



        void IntegratedQueue::expandVertex(const VertexPtr& vertex)
        {
            //Should we expand this vertex?
            if (this->vertexPruneCondition(vertex) == false)
            {
                //Variables:
                //The vector of nearby samples (either within r or the k-nearest)
                std::vector<VertexPtr> neighbourSamples;

                //Get the set of nearby free states:
                nearSamplesFunc_(vertex, &neighbourSamples);

                //Iterate over the vector and add only those who could ever provide a better solution:
                for (unsigned int i = 0u; i < neighbourSamples.size(); ++i)
                {
                    //Attempt to queue the edge
                    this->queueupEdge(vertex, neighbourSamples.at(i));
                }

                //If it is a new vertex, we also add rewiring candidates:
                if (vertex->isNew() == true)
                {
                    //Variables:
                    //The vector of vertices within r of the vertexf
                    std::vector<VertexPtr> neighbourVertices;

                    //Get the set of nearby free states:
                    nearVerticesFunc_(vertex, &neighbourVertices);

                    //Iterate over the vector and add only those who could ever provide a better solution:
                    for (unsigned int i = 0u; i < neighbourVertices.size(); ++i)
                    {
                        //Make sure it is not the root or myself.
                        if (neighbourVertices.at(i)->isRoot() == false && neighbourVertices.at(i) != vertex)
                        {
                            //Make sure I am not already the parent or child
                            if (neighbourVertices.at(i)->getParent() != vertex && neighbourVertices.at(i) != vertex->getParent())
                            {
                                //Attempt to queue the edge:
                                this->queueupEdge(vertex, neighbourVertices.at(i));
                            }
                            //No else
                        }
                        //No else
                    }

                    //Mark the vertex as old
                    vertex->markOld();
                }
                //No else
            }
            //No else
        }



        void IntegratedQueue::queueupEdge(const VertexPtr& parent, const VertexPtr& child)
        {
            //Variables:
            //A bool to store the conditional failed edge check
            bool previouslyFailed;

            //See if we're checking for previous failure:
            if (useFailureTracking_ == true)
            {
                previouslyFailed = parent->hasAlreadyFailed(child);
            }
            else
            {
                previouslyFailed = false;
            }

            //Make sure the edge has not already failed
            if (previouslyFailed == false)
            {
                //Variable:
                //The edge:
                vertex_pair_t newEdge;

                //Make the edge
                newEdge = std::make_pair(parent, child);

                //Should this edge be in the queue? I.e., is it *not* due to be pruned:
                if (this->edgePruneCondition(newEdge) == false)
                {
                    this->edgeInsertHelper(newEdge, edgeQueue_.end());
                }
                //No else, we assume that it's better to calculate this condition multiple times than have the list of failed sets become too large...?
            }
            //No else
        }





        void IntegratedQueue::reinsertVertex(const VertexPtr& unorderedVertex)
        {
            //Variables:
            //Whether the vertex is expanded.
            bool alreadyExpanded;
            //My entry in the vertex lookup:
            vid_vertex_queue_iter_umap_t::iterator myLookup;
            //The list of edges from the vertex:
            vid_edge_queue_iter_umap_t::iterator edgeItersFromVertex;

            //Get my iterator:
            myLookup = vertexIterLookup_.find(unorderedVertex->getId());

            //Assert
            if (myLookup == vertexIterLookup_.end())
            {
                throw ompl::Exception("Vertex to reinsert is not in the lookup. Something went wrong.");
            }

            //Test if it I am currently expanded.
            if (vertexToExpand_ == vertexQueue_.end())
            {
                //The token is at the end, therefore this vertex is in front of it:
                alreadyExpanded = true;
            }
            else if ( this->vertexQueueComparison(myLookup->second->first, vertexToExpand_->first) == true )
            {
                //The vertexQueueCondition says that this vertex was enterted with a cost that is in front of the current token:
                alreadyExpanded = true;
            }
            else
            {
                //Otherwise I have not been expanded yet.
                alreadyExpanded = false;
            }

            //Remove myself, not touching my lookup entries
            this->vertexRemoveHelper(unorderedVertex, vertex_nn_ptr_t(), vertex_nn_ptr_t(), false);

            //Reinsert myself, expanding if I cross the token if I am not already expanded
            this->vertexInsertHelper(unorderedVertex, alreadyExpanded == false);

            //Iterate over my outgoing edges and reinsert them in the queue:
            //Get my list of outgoing edges
            edgeItersFromVertex = outgoingEdges_.find(unorderedVertex->getId());

            //Reinsert the edges:
            if (edgeItersFromVertex != outgoingEdges_.end())
            {
                //Variables
                //The iterators to the edge queue from this vertex
                edge_queue_iter_list_t edgeItersToResort;

                //Copy the iters to resort
                edgeItersToResort = edgeItersFromVertex->second;

                //Clear the outgoing lookup
                edgeItersFromVertex->second =  edge_queue_iter_list_t();

                //Iterate over the list of iters to resort, inserting each one as a new edge, and then removing it as an iterator from the edge queue and the incoming lookup
                for (edge_queue_iter_list_t::iterator resortIter = edgeItersToResort.begin(); resortIter != edgeItersToResort.end(); ++resortIter)
                {
                    //Check if the edge should be reinserted
                    if ( this->edgePruneCondition((*resortIter)->second) == false )
                    {
                        //Call helper to reinsert. Looks after lookups, hint at the location it's coming out of
                        this->edgeInsertHelper( (*resortIter)->second, *resortIter );
                    }
                    //No else, prune.

                    //Remove the old edge and its entry in the incoming lookup. No need to remove from this lookup, as that's been cleared:
                    this->edgeRemoveHelper(*resortIter, true, false);
                }
            }
            //No else, no edges from this vertex to requeue
        }



        std::pair<unsigned int, unsigned int> IntegratedQueue::pruneBranch(const VertexPtr& branchBase, const vertex_nn_ptr_t& vertexNN, const vertex_nn_ptr_t& freeStateNN)
        {
            //We must iterate over the children of this vertex and prune each one.
            //Then we must decide if this vertex (a) gets deleted or (b) placed back on the sample set.
            //(a) occurs if it has a lower-bound heuristic greater than the current solution
            //(b) occurs if it doesn't.

            //Some asserts:
            if (branchBase == goalVertex_)
            {
                throw ompl::Exception("Trying to prune goal vertex. Something went wrong.");
            }

            if (branchBase == startVertex_ )
            {
                throw ompl::Exception("Trying to prune start vertex. Something went wrong.");
            }

            if (branchBase->isConnected() == false)
            {
                throw ompl::Exception("Trying to prune a disconnected vertex. Something went wrong.");
            }

            //Variables:
            //The counter of vertices and samples pruned:
            std::pair<unsigned int, unsigned int> numPruned;
            //The vector of my children:
            std::vector<VertexPtr> children;

            //Initialize the counter:
            numPruned = std::make_pair(1u, 0u);

            //Disconnect myself from my parent, not cascading costs as I know my children are also being disconnected:
            this->disconnectParent(branchBase, false);

            //Get the vector of children
            branchBase->getChildren(&children);

            //Remove myself from everything:
            numPruned.second = this->vertexRemoveHelper(branchBase, vertexNN, freeStateNN, true);

            //Prune my children:
            for (unsigned int i = 0u; i < children.size(); ++i)
            {
                //Variable:
                //The number pruned by my children:
                std::pair<unsigned int, unsigned int> childNumPruned;

                //Prune my children:
                childNumPruned = this->pruneBranch(children.at(i), vertexNN, freeStateNN);

                //Update my counter:
                numPruned.first = numPruned.first + childNumPruned.first;
                numPruned.second = numPruned.second + childNumPruned.second;
            }

            //Return the number pruned
            return numPruned;
        }



        void IntegratedQueue::disconnectParent(const VertexPtr& oldVertex, bool cascadeCostUpdates)
        {
            if (oldVertex->hasParent() == false)
            {
                throw ompl::Exception("An orphaned vertex has been passed for disconnection. Something went wrong.");
            }

            //Check if my parent has already been pruned. This can occur if we're cascading vertex disconnections.
            if (oldVertex->getParent()->isPruned() == false)
            {
                //If not, remove myself from my parent's list of children, not updating down-stream costs
                oldVertex->getParent()->removeChild(oldVertex, false);
            }

            //Remove my parent link, cascading cost updates if requested:
            oldVertex->removeParent(cascadeCostUpdates);
        }



        void IntegratedQueue::vertexInsertHelper(const VertexPtr& newVertex, bool expandIfBeforeToken)
        {
            //Variable:
            //The iterator to the new edge in the queue:
            vertex_queue_iter_t vertexIter;

            //Insert into the order map, getting the interator
            vertexIter = vertexQueue_.insert( std::make_pair(this->vertexQueueValue(newVertex), newVertex) );

            //Store the iterator in the lookup. This will create insert if necessary and otherwise lookup
            vertexIterLookup_[newVertex->getId()] = vertexIter;

            //Check if we are in front of the token and expand if so:
            if (vertexQueue_.size() == 1u)
            {
                //If the vertex queue is now of size 1, that means that this was the first vertex. Set the token to it and don't even think of expanding anything:
                vertexToExpand_ = vertexQueue_.begin();
            }
            else if (expandIfBeforeToken == true)
            {
                /*
                There are 3ish cases:
                    1 The new vertex is immediately before the token.
                        a The token is not at the end: Don't expand and shift the token to the new vertex.
                        b The token is at the end: Don't expand and shift the token to the new vertex.
                    2 The new vertex is before the token, but *not* immediately (i.e., there are vertices between it):
                        a The token is at the end: Expand the vertex
                        b The token is not at the end: Expand the vertex
                    3 The new vertex is after the token: Don't expand. It cleanly goes into the list of vertices to expand
                Note: By shifting the token, we assure that if the new vertex is better than the best edge, it will get expanded on the next pop.

                The cases look like this (-: expanded vertex, x: unexpanded vertex, X: token (next to expand), *: new vertex):
                We represent the token at the end with no X in the line:

                    1a: ---*Xxx   ->   ---Xxxx
                    1b: ------*   ->   ------X
                    2a: ---*---   ->   -------
                    2b: --*-Xxx   ->   ----Xxx
                    3: ---Xx*x   ->   ---Xxxx
                */

                //Variable:
                //The vertex before the token. Remember that since we have already added the new vertex, this could be ourselves:
                vertex_queue_iter_t preToken;

                //Get the vertex before the current token:
                preToken = vertexToExpand_;
                --preToken;

                //Check if we are immediately before: (1a & 1b)
                if (preToken == vertexIter)
                {
//                    if (vertexToExpand_ != vertexQueue_.end())
//                    {
//                        std::cout << "1a: " << vertexToExpand_->first.value() << " -> " << vertexIter->first.value() << std::endl;
//                    }
//                    else
//                    {
//                        std::cout << "1b: infty  -> " << vertexIter->first.value() << std::endl;
//                    }
                    //The vertex before the token is the newly added vertex. Therefore we can just move the token up to the newly added vertex:
                    vertexToExpand_ = vertexIter;
                }
                else
                {
                    //We are not immediately before the token.

                    //Check if the token is at the end (2a)
                    if (vertexToExpand_ == vertexQueue_.end())
                    {
                        //It is. We've expanded the whole queue, and the new vertex isn't at the end of the queue. Expand!
                        this->expandVertex(newVertex);
//                        std::cout << "2a: " << vertexIter->first.value() << " < infty" << std::endl;
                    }
                    else
                    {
                        //The token is not at the end. That means we can safely dereference it:
                        //Are we in front of it (2b)?
                        if ( this->vertexQueueComparison(this->vertexQueueValue(newVertex), vertexToExpand_->first) == true )
                        {
                            //We're before it, so expand it:
                            this->expandVertex(newVertex);
//                            std::cout << "2b: " << vertexIter->first.value() << " < " << vertexToExpand_->first.value() << std::endl;
                        }
//                        else
//                        {
//                            std::cout << "3: " << vertexIter->first.value() << " > " << vertexToExpand_->first.value() << std::endl;
//                        }
                        //No else, the vertex is behind the current token (3) and will get expanded as necessary.
                    }
                }
            }
        }



        unsigned int IntegratedQueue::vertexRemoveHelper(VertexPtr oldVertex, const vertex_nn_ptr_t& vertexNN, const vertex_nn_ptr_t& freeStateNN, bool removeLookups)
        {
            //Variable
            //The number of samples deleted (i.e., if this vertex is NOT moved to a sample, this is a 1)
            unsigned int deleted;

            //Check that the vertex is not connected to a parent:
            if (oldVertex->hasParent() == true && removeLookups == true)
            {
                throw ompl::Exception("Cannot delete a vertex connected to a parent unless the vertex is being immediately reinserted, in which case removeLookups should be false.");
            }

            //Start undeleted:
            deleted = 0u;

            //Check if there's anything to delete:
            if (vertexQueue_.empty() == false)
            {
                //Variable
                //The iterator into the lookup:
                vid_vertex_queue_iter_umap_t::iterator lookupIter;

                //Get my lookup iter:
                lookupIter = vertexIterLookup_.find(oldVertex->getId());

                //Assert
                if (lookupIter == vertexIterLookup_.end())
                {
                    std::cout << std::endl << "vId: " << oldVertex->getId() << std::endl;
                    throw ompl::Exception("Deleted vertex is not found in lookup. Something went wrong.");
                }

                //Check if we need to move the expansion token:
                if (lookupIter->second == vertexToExpand_)
                {
                    //It is the token, move it to the next:
                    ++vertexToExpand_;
                }
                //No else, not the token.

                //Remove myself from the vertex queue:
                vertexQueue_.erase(lookupIter->second);

                //Remove from lookups map as requested
                if (removeLookups == true)
                {
                    vertexIterLookup_.erase(lookupIter);
                    this->removeEdgesFrom(oldVertex);
                }

                //Check if I have been given permission to change sets:
                if (bool(vertexNN) == true && bool(freeStateNN) == true)
                {
                    //Check if I should be discarded completely:
                    if (this->samplePruneCondition(oldVertex) == true)
                    {
                        //Yes, the vertex isn't even useful as a sample
                        //Update the counter:
                        deleted = 1u;

                        //Remove from the incoming edge container if requested:
                        if (removeLookups == true)
                        {
                            this->removeEdgesTo(oldVertex);
                        }

                        //Remove myself from the nearest neighbour structure:
                        vertexNN->remove(oldVertex);

                        //Finally, mark as pruned. This is a lock that can never be undone and prevents accessing anything about the vertex.
                        oldVertex->markPruned();
                    }
                    else
                    {
                        //No, the vertex is still useful as a sample:
                        //Remove myself from the nearest neighbour structure:
                        vertexNN->remove(oldVertex);

                        //And add the vertex to the set of samples, keeping the incoming edges:
                        freeStateNN->add(oldVertex);
                    }
                }
                //Else, if I was given null pointers to the NN structs, that's because this sample is not allowed to change sets.
            }
            else
            {
                std::cout << std::endl << "vId: " << oldVertex->getId() << std::endl;
                throw ompl::Exception("Removing a nonexistent vertex.");
            }

            //Return if the sample was deleted:
            return deleted;
        }



        void IntegratedQueue::edgeInsertHelper(const vertex_pair_t& newEdge, edge_queue_iter_t positionHint)
        {
            //Variable:
            //The iterator to the new edge in the queue:
            edge_queue_iter_t edgeIter;

            //Insert into the edge queue, getting the iter
            if (positionHint == edgeQueue_.end())
            {
                //No hint, insert:
                edgeIter = edgeQueue_.insert(std::make_pair(this->edgeQueueValue(newEdge), newEdge));
            }
            else
            {
                //Insert with hint:
                edgeIter = edgeQueue_.insert(positionHint, std::make_pair(this->edgeQueueValue(newEdge), newEdge));
            }

            if (outgoingLookupTables_ == true)
            {
                //Push the newly created edge back on the list of edges from the parent.
                //The [] return an reference to the existing entry, or create a new entry:
                outgoingEdges_[newEdge.first->getId()].push_back(edgeIter);
            }

            if (incomingLookupTables_ == true)
            {
                //Push the newly created edge back on the list of edges from the child.
                //The [] return an reference to the existing entry, or create a new entry:
                incomingEdges_[newEdge.second->getId()].push_back(edgeIter);
            }
        }



        void IntegratedQueue::edgeRemoveHelper(const edge_queue_iter_t& oldEdgeIter, bool rmIncomingLookup, bool rmOutgoingLookup)
        {
            //Erase the lookup tables:
            if (rmIncomingLookup == true)
            {
                //Erase the entry in the outgoing lookup table:
                this->rmIncomingLookup(oldEdgeIter);
            }
            //No else

            if (rmOutgoingLookup == true)
            {
                //Erase  the entry in the ingoing lookup table:
                this->rmOutgoingLookup(oldEdgeIter);
            }
            //No else

            //Finally erase from the queue:
            edgeQueue_.erase(oldEdgeIter);
        }



        void IntegratedQueue::rmIncomingLookup(const edge_queue_iter_t& mmapIterToRm)
        {
            if (incomingLookupTables_ == true)
            {
                this->rmEdgeLookupHelper(incomingEdges_, mmapIterToRm->second.second->getId(), mmapIterToRm);
            }
            //No else
        }



        void IntegratedQueue::rmOutgoingLookup(const edge_queue_iter_t& mmapIterToRm)
        {
            if (outgoingLookupTables_ == true)
            {
                this->rmEdgeLookupHelper(outgoingEdges_, mmapIterToRm->second.first->getId(), mmapIterToRm);
            }
            //No else
        }



        void IntegratedQueue::rmEdgeLookupHelper(vid_edge_queue_iter_umap_t& lookup, const Vertex::id_t& idx, const edge_queue_iter_t& mmapIterToRm)
        {
            //Variable:
            //An iterator to the vertex,list pair in the lookup
            vid_edge_queue_iter_umap_t::iterator iterToVertexListPair;

            //Get the list in the lookup for the given index:
            iterToVertexListPair = lookup.find(idx);

            //Make sure it was actually found before derefencing it:
            if (iterToVertexListPair != lookup.end())
            {
                //Variable:
                //Whether I've found the mmapIterToRm in my list:
                bool found;
                //The iterator to the mmapIterToRm in my list:
                edge_queue_iter_list_t::iterator iterToList;

                //Start at the front:
                iterToList = iterToVertexListPair->second.begin();

                //Iterate through the list and find mmapIterToRm
                found = false;
                while(found == false && iterToList != iterToVertexListPair->second.end())
                {
                    //Compare the value in the list to the target:
                    if (*iterToList == mmapIterToRm)
                    {
                        //Mark as found:
                        found = true;
                    }
                    else
                    {
                        //Increment the iterator:
                        ++iterToList;
                    }
                }

                if (found == true)
                {
                    iterToVertexListPair->second.erase(iterToList);
                }
                else
                {
                    throw ompl::Exception("Edge iterator not found under given index in lookup hash.");
                }
            }
            else
            {
                throw ompl::Exception("Indexing vertex not found in lookup hash.");
            }
        }



















        ompl::base::Cost IntegratedQueue::vertexQueueValue(const VertexPtr& vertex) const
        {
            return currentHeuristicVertexFunc_(vertex);
        }



        IntegratedQueue::cost_pair_t IntegratedQueue::edgeQueueValue(const vertex_pair_t& edge) const
        {
            return std::make_pair(currentHeuristicEdgeFunc_(edge), edge.first->getCost());
        }



        bool IntegratedQueue::vertexQueueComparison(const ompl::base::Cost& lhs, const ompl::base::Cost& rhs) const
        {
            //lhs < rhs?
            return this->isCostBetterThan(lhs, rhs);
        }



        bool IntegratedQueue::edgeQueueComparison(const cost_pair_t& lhs, const cost_pair_t& rhs) const
        {
            bool lhsLTrhs;

            //Get if LHS is less than RHS.
            lhsLTrhs = this->isCostBetterThan(lhs.first, rhs.first);

            //If it's not, it could be equal
            if (lhsLTrhs == false)
            {
                //If RHS is also NOT less than LHS, than they're equal and we need to check the second key
                if (this->isCostBetterThan(rhs.first, lhs.first) == false)
                {
                    //lhs == rhs
                    //Compare their second values
                    lhsLTrhs = this->isCostBetterThan( lhs.second, rhs.second );
                }
                //No else: lhs > rhs
            }
            //No else, lhs < rhs

            return lhsLTrhs;
        }



        bool IntegratedQueue::isCostBetterThan(const ompl::base::Cost& a, const ompl::base::Cost& b) const
        {
            return a.value() < b.value();
        }



        bool IntegratedQueue::isCostWorseThan(const ompl::base::Cost& a, const ompl::base::Cost& b) const
        {
            //If b is better than a, then a is worse than b
            return this->isCostBetterThan(b, a);
        }



        bool IntegratedQueue::isCostEquivalentTo(const ompl::base::Cost& a, const ompl::base::Cost& b) const
        {
            //If a is not better than b, and b is not better than a, then they are equal
            return !this->isCostBetterThan(a,b) && !this->isCostBetterThan(b,a);
        }



        bool IntegratedQueue::isCostNotEquivalentTo(const ompl::base::Cost& a, const ompl::base::Cost& b) const
        {
            //If a is better than b, or b is better than a, then they are not equal
            return this->isCostBetterThan(a,b) || this->isCostBetterThan(b,a);
        }



        bool IntegratedQueue::isCostBetterThanOrEquivalentTo(const ompl::base::Cost& a, const ompl::base::Cost& b) const
        {
            //If b is not better than a, then a is better than, or equal to, b
            return !this->isCostBetterThan(b, a);
        }



        bool IntegratedQueue::isCostWorseThanOrEquivalentTo(const ompl::base::Cost& a, const ompl::base::Cost& b) const
        {
            //If a is not better than b, than a is worse than, or equal to, b
            return !this->isCostBetterThan(a,b);
        }



        void IntegratedQueue::setUseFailureTracking(bool trackFailures)
        {
            useFailureTracking_ = trackFailures;
        }



        bool IntegratedQueue::getUseFailureTracking() const
        {
            return useFailureTracking_;
        }
    } // geometric
} //ompl
