"""
Computes shortest paths from a single source vertex to all other vertices.
Supports negative edge weights and detects negative-weight cycles.

:param vertices: List of all vertex identifiers, e.g., ['A', 'B', 'C']
:param edges: List of tuples (u, v, w) representing a directed edge from u to v with weight w
:param source: The starting vertex identifier
:return: A dictionary mapping each vertex to its shortest distance from the source
:raises ValueError: If the graph contains a negative-weight cycle reachable from the source
"""

import math


def main(nodes, edges, source) -> dict:
    distances = {}
    for node in nodes:
        distances[node] = math.inf

    distances[source] = 0
    for _ in range(len(nodes) - 1):
        for src, dest, weight in edges:
            if distances[src] + weight < distances[dest]:
                distances[dest] = distances[src] + weight

    for src, dest, weight in edges:
        if distances[src] + weight < distances[dest]:
            raise Exception("The grapth has negative total weight cycle")
    return distances


nodes = ["A", "B", "C", "D", "E"]
graph_edges = [
    ("A", "B", 6),
    ("A", "C", 7),
    ("B", "D", 5),
    ("B", "E", -4),
    ("C", "D", -3),
    ("D", "B", -2),
    ("E", "D", 7),
]

print(main(nodes, graph_edges, "A"))

nodes = ["X", "Y", "Z", "W"]
graph_edges = [
    ("X", "Y", -2),
    ("Y", "Z", -3),
    ("Z", "W", -1),
]
print(main(nodes, graph_edges, "X"))

nodes = ["A", "B", "C", "D"]
graph_edges = [
    ("A", "B", 4),
    ("B", "C", 2),
    ("D", "C", 1),
]
print(main(nodes, graph_edges, "A"))

nodes = ["P", "Q", "R", "S"]
graph_edges = [
    ("P", "Q", 4),
    ("Q", "R", -8),
    ("R", "P", 2),
    ("R", "S", 3),
]
print(main(nodes, graph_edges, "P"))
