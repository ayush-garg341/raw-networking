"""
Why this algo works? It never backtracks.

By always building our path out from the absolute closest known point,
we guarantee that the moment we mark a city as "visited,"
we have found the absolute best way to get there.
"""

import heapq
import math


def main(source: str, network: dict) -> dict:
    cost_from_source = {}
    nodes = []
    visited = set()

    # Assuming the distance from source is inf
    for k, _ in network.items():
        cost_from_source[k] = math.inf

    # Source will have distance 0 from itself
    cost_from_source[source] = 0

    # Cost is 0 for the source
    nodes.append((0, source))

    # Checking until we have exhausted all the neighbour nodes from the source
    while nodes:
        cost, node = heapq.heappop(nodes)

        # If node is visited then continue
        if node in visited:
            continue

        # Else add in visited set
        visited.add(node)

        for neighbour, road_cost in network[node]:
            # if A is neighbour of C then C is also neighbour of A
            if neighbour not in visited:
                new_cost = cost + road_cost

                if new_cost < cost_from_source[neighbour]:
                    cost_from_source[neighbour] = new_cost
                    heapq.heappush(nodes, (new_cost, neighbour))

    return cost_from_source


graph_1 = {
    "A": [("B", 4), ("C", 2)],
    "B": [("A", 4), ("D", 1), ("E", 2)],
    "C": [("A", 2), ("D", 5), ("G", 6)],
    "D": [("B", 1), ("C", 5), ("E", 3), ("G", 1)],
    "E": [("B", 2), ("D", 3), ("F", 3), ("G", 2)],
    "F": [("E", 3), ("G", 1)],
    "G": [("C", 6), ("D", 1), ("E", 2), ("F", 1)],
}
source_1 = "A"

print(main(source_1, graph_1))

graph_2 = {
    "A": [("B", 1), ("D", 5)],
    "B": [("A", 1), ("D", 3)],
    "D": [("A", 5), ("B", 3)],
    "E": [("F", 10)],
    "F": [("E", 10)],
}
source_2 = "A"
print(main(source_2, graph_2))

graph_3 = {
    "A": [("B", 2), ("H", 2)],
    "B": [("A", 2), ("C", 4), ("E", 1)],
    "C": [("B", 4), ("D", 2), ("F", 1)],
    "D": [("C", 2), ("G", 2)],
    "E": [("B", 1), ("H", 1), ("F", 2)],
    "F": [("C", 1), ("E", 2), ("G", 1)],
    "G": [("D", 2), ("F", 1), ("H", 4)],
    "H": [("A", 2), ("E", 1), ("G", 4)],
}
source_3 = "A"

print(main(source_3, graph_3))
