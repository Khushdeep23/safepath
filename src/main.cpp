// Risk scores are subjective estimates based on road class and local knowledge, not official data.
#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <queue>
#include <string>
#include <limits>
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>
using namespace std;


class Location {
private:
    int id;
    string name;
    double latitude;
    double longitude;
    int populationTier;
    bool safeSpot;     
    bool tricity;
    bool detailed;
public:
    Location(int id, string name, double latitude, double longitude, int populationTier,
             bool safeSpot = false, bool tricity = false, bool detailed = false)
        : id(id), name(name), latitude(latitude), longitude(longitude),
          populationTier(populationTier), safeSpot(safeSpot), tricity(tricity),
          detailed(detailed) {}
    int getId() const { return id; }
    string getName() const { return name; }
    double getLatitude() const { return latitude; }
    double getLongitude() const { return longitude; }
    int getPopulationTier() const { return populationTier; }
    bool isSafeSpot() const { return safeSpot; }
    bool isTricity() const { return tricity; }
    bool isDetailed() const { return detailed; }
};


class GeoUtils {
public:
    static double haversineDistance(const Location& a, const Location& b) {
        const double earthRadiusKm = 6371.0;   // R
        const double radians = 3.14159265358979323846 / 180.0;  //TRIG SIN/COS IS IN RADIANS SO CONVERSION
        const double lat1 = a.getLatitude() * radians;   // LAT1 IS CONVERTED INTO RADIANS
        const double lat2 = b.getLatitude() * radians;
        const double dLat = (b.getLatitude() - a.getLatitude()) * radians;  //DONO CITIES KE LAT KA DIFF
        const double dLon = (b.getLongitude() - a.getLongitude()) * radians; // DONO CITIES KE LONG KA DIFF
        const double sinLat = sin(dLat / 2.0);
        const double sinLon = sin(dLon / 2.0);
        const double hav = min(1.0, max(0.0,  //CALC OF H 
            sinLat * sinLat + cos(lat1) * cos(lat2) * sinLon * sinLon));
        return earthRadiusKm * 2.0 * atan2(sqrt(hav), sqrt(1.0 - hav));  //RETURNING OF DISTANCE
    }
};


class Road {
private:
    int from, to;
    double distance;   
    double risk;      
public:
    Road(int from, int to, double distance, double risk)
        : from(from), to(to), distance(distance), risk(risk) {}
    int getFrom() const { return from; }
    int getTo() const { return to; }
    double getDistance() const { return distance; }
    double getRisk() const { return risk; }
};

class Graph;

// ---------- Explainable, deterministic road risk estimates ----------
class RiskModel {
private:
    static bool isBorderTown(const string& name) {
        return name == "Abohar" || name == "Fazilka" || name == "Ferozepur"
            || name == "Jalalabad" || name == "Zira";
    }
public:
    static double distanceFactor(const Location& a, const Location& b, bool highway) {
        (void)a;
        (void)b;
        (void)highway;
        return 1.25;
    }
    static double baseRisk(const Location& a, const Location& b, bool highway) {
        if (highway) {
            return a.getPopulationTier() == 1 && b.getPopulationTier() == 1 ? 1.0 : 2.0;
        }
        if (isBorderTown(a.getName()) || isBorderTown(b.getName())) {
            return 6.0;
        }
        if (a.getName().find("Industrial Area") != string::npos
            || b.getName().find("Industrial Area") != string::npos) {
            return 4.0;
        }
        if (a.getPopulationTier() == 3 || b.getPopulationTier() == 3) {
            return 5.0;
        }
        if (a.getName() == "Kharar" || b.getName() == "Kharar"
            || a.getName() == "Landran" || b.getName() == "Landran"
            || a.getName() == "Mullanpur" || b.getName() == "Mullanpur"
            || a.getName() == "Zirakpur" || b.getName() == "Zirakpur"
            || a.getName() == "Chandigarh University (Gharuan)"
            || b.getName() == "Chandigarh University (Gharuan)") {
            return 4.0;
        }
        return 3.0;
    }
    static void applyLocalKnowledge(Graph& graph);
};

// ---------- The map (graph using adjacency list) ----------
class Graph {
private:
    vector<Location> locations;
    vector<vector<Road>> adj;
    vector<Road> allRoads;
    set<pair<int, int>> roadKeys;
    set<int> explicitNetworkLocations;

    vector<bool> breadthFirstReachable(int start) const {
        vector<bool> visited(locations.size(), false);
        queue<int> pending;
        visited[start] = true;
        pending.push(start);
        while (!pending.empty()) {
            const int current = pending.front();
            pending.pop();
            for (size_t i = 0; i < adj[current].size(); i++) {
                const int next = adj[current][i].getTo();
                if (!visited[next]) {
                    visited[next] = true;
                    pending.push(next);
                }
            }
        }
        return visited;
    }
public:
    void addLocation(const Location& loc) {
        locations.push_back(loc);
        adj.push_back(vector<Road>());
    }
    void addRoad(int a, int b, bool highway = false) {
        const pair<int, int> key(min(a, b), max(a, b));
        const double risk = RiskModel::baseRisk(locations[a], locations[b], highway);
        const double distanceFactor = RiskModel::distanceFactor(
            locations[a], locations[b], highway);
        if (roadKeys.find(key) != roadKeys.end()) {
            if (highway) {
                setRoadRisk(a, b, risk);
                const double rawDistance = GeoUtils::haversineDistance(locations[a], locations[b])
                    * distanceFactor;
                setRoadDistance(a, b, round(rawDistance * 10.0) / 10.0);
            }
            return;
        }
        const double rawDistance = GeoUtils::haversineDistance(locations[a], locations[b])
            * distanceFactor;
        const double distance = round(rawDistance * 10.0) / 10.0;
        adj[a].push_back(Road(a, b, distance, risk));
        adj[b].push_back(Road(b, a, distance, risk));
        allRoads.push_back(Road(a, b, distance, risk));
        roadKeys.insert(key);
    }
    void connectNearestTowns() {
        for (size_t i = 0; i < locations.size(); i++) {
            if (explicitNetworkLocations.find(static_cast<int>(i))
                != explicitNetworkLocations.end()) continue;
            vector<pair<double, int>> nearest;
            for (size_t j = 0; j < locations.size(); j++) {
                if (i == j) continue;
                if (explicitNetworkLocations.find(static_cast<int>(j))
                    != explicitNetworkLocations.end()) continue;
                nearest.push_back(make_pair(
                    GeoUtils::haversineDistance(locations[i], locations[j]),
                    static_cast<int>(j)));
            }
            sort(nearest.begin(), nearest.end());
            const size_t count = min(static_cast<size_t>(4), nearest.size());
            for (size_t j = 0; j < count; j++) {
                const int other = nearest[j].second;
                const double roadDistance = GeoUtils::haversineDistance(locations[i], locations[other])
                    * RiskModel::distanceFactor(locations[i], locations[other], false);
                if (roadDistance <= 70.0) {
                    addRoad(static_cast<int>(i), other);
                }
            }
        }
    }
    void addHighwayRoad(const string& fromName, const string& toName) {
        const int from = findLocationId(fromName);
        const int to = findLocationId(toName);
        if (from < 0 || to < 0) {
            throw runtime_error("Highway link references an unknown town: "
                                + fromName + " - " + toName);
        }
        addRoad(from, to, true);
    }
    void addLocalRoad(const string& fromName, const string& toName) {
        const int from = findLocationId(fromName);
        const int to = findLocationId(toName);
        if (from < 0 || to < 0) {
            throw runtime_error("Local road references an unknown location: "
                                + fromName + " - " + toName);
        }
        addRoad(from, to);
    }
    void markExplicitNetworkLocation(const string& name) {
        const int id = findLocationId(name);
        if (id < 0) {
            throw runtime_error("Explicit road network references an unknown location: " + name);
        }
        explicitNetworkLocations.insert(id);
    }
    void ensureConnected() {
        vector<bool> connected = breadthFirstReachable(0);
        while (find(connected.begin(), connected.end(), false) != connected.end()) {
            double shortest = numeric_limits<double>::max();
            int connectedTown = -1;
            int separateTown = -1;
            for (size_t i = 0; i < locations.size(); i++) {
                if (!connected[i]) continue;
                if (explicitNetworkLocations.find(static_cast<int>(i))
                    != explicitNetworkLocations.end()) continue;
                for (size_t j = 0; j < locations.size(); j++) {
                    if (connected[j]) continue;
                    if (explicitNetworkLocations.find(static_cast<int>(j))
                        != explicitNetworkLocations.end()) continue;
                    const double distance = GeoUtils::haversineDistance(locations[i], locations[j])
                        * RiskModel::distanceFactor(locations[i], locations[j], false);
                    if (distance > 70.0) continue;
                    if (distance < shortest) {
                        shortest = distance;
                        connectedTown = static_cast<int>(i);
                        separateTown = static_cast<int>(j);
                    }
                }
            }
            if (connectedTown < 0 || separateTown < 0) {
                throw runtime_error("Unable to connect all towns in the road graph.");
            }
            const double roadDistance = GeoUtils::haversineDistance(
                locations[connectedTown], locations[separateTown])
                * RiskModel::distanceFactor(
                    locations[connectedTown], locations[separateTown], false);
            if (roadDistance > 70.0) {
                throw runtime_error("Nearest connectivity link exceeds the 70 km non-highway limit.");
            }
            addRoad(connectedTown, separateTown);
            connected = breadthFirstReachable(0);
        }
    }
    void setRoadRisk(int a, int b, double risk) {
        const pair<int, int> key(min(a, b), max(a, b));
        if (roadKeys.find(key) == roadKeys.end()) return;
        for (size_t i = 0; i < allRoads.size(); i++) {
            if (min(allRoads[i].getFrom(), allRoads[i].getTo()) == key.first
                && max(allRoads[i].getFrom(), allRoads[i].getTo()) == key.second) {
                allRoads[i] = Road(allRoads[i].getFrom(), allRoads[i].getTo(),
                                   allRoads[i].getDistance(), risk);
                break;
            }
        }
        for (size_t endpoint = 0; endpoint < 2; endpoint++) {
            const int from = endpoint == 0 ? a : b;
            const int to = endpoint == 0 ? b : a;
            for (size_t i = 0; i < adj[from].size(); i++) {
                if (adj[from][i].getTo() == to) {
                    adj[from][i] = Road(from, to, adj[from][i].getDistance(), risk);
                    break;
                }
            }
        }
    }
    void setRoadDistance(int a, int b, double distance) {
        const pair<int, int> key(min(a, b), max(a, b));
        if (roadKeys.find(key) == roadKeys.end()) return;
        for (size_t i = 0; i < allRoads.size(); i++) {
            if (min(allRoads[i].getFrom(), allRoads[i].getTo()) == key.first
                && max(allRoads[i].getFrom(), allRoads[i].getTo()) == key.second) {
                allRoads[i] = Road(allRoads[i].getFrom(), allRoads[i].getTo(),
                                   distance, allRoads[i].getRisk());
                break;
            }
        }
        for (size_t endpoint = 0; endpoint < 2; endpoint++) {
            const int from = endpoint == 0 ? a : b;
            const int to = endpoint == 0 ? b : a;
            for (size_t i = 0; i < adj[from].size(); i++) {
                if (adj[from][i].getTo() == to) {
                    adj[from][i] = Road(from, to, distance, adj[from][i].getRisk());
                    break;
                }
            }
        }
    }
    void setRisksForRoadsTouching(const string& townName, double risk, const string& reason) {
        const int townId = findLocationId(townName);
        if (townId < 0) return;
        for (size_t i = 0; i < allRoads.size(); i++) {
            const Road& road = allRoads[i];
            if (road.getFrom() != townId && road.getTo() != townId) continue;
            setRoadRisk(road.getFrom(), road.getTo(), risk);
            cout << "  " << reason << ": " << locations[road.getFrom()].getName()
                 << " - " << locations[road.getTo()].getName()
                 << " = " << risk << "/10\n";
        }
    }
    int size() const { return locations.size(); }
    bool isConnected() const {
        if (locations.empty()) return true;
        const vector<bool> connected = breadthFirstReachable(0);
        return find(connected.begin(), connected.end(), false) == connected.end();
    }
    const vector<Road>& getRoadsFrom(int u) const { return adj[u]; }
    const vector<Location>& getLocations() const { return locations; }
    const vector<Road>& getAllRoads() const { return allRoads; }
    int findLocationId(const string& name) const {
        for (size_t i = 0; i < locations.size(); i++) {
            if (locations[i].getName() == name) return locations[i].getId();
        }
        return -1;
    }
};

class LocalRiskOverride {
private:
    string location;
    double risk;
    string reason;
public:
    LocalRiskOverride(const string& location, double risk, const string& reason)
        : location(location), risk(risk), reason(reason) {}
    string getLocation() const { return location; }
    double getRisk() const { return risk; }
    string getReason() const { return reason; }
};

void RiskModel::applyLocalKnowledge(Graph& graph) {
    const LocalRiskOverride overrides[] = {
        LocalRiskOverride("Garhshankar", 8.0, "Quiet and isolated Garhshankar roads"),
        LocalRiskOverride("Anandpur Sahib", 8.0, "Quiet and isolated Anandpur Sahib roads"),
        LocalRiskOverride("Nangal", 8.0, "Quiet and isolated Nangal roads"),
        LocalRiskOverride("Fazilka", 7.0, "Fazilka border-area roads"),
        LocalRiskOverride("Abohar", 7.0, "Abohar border-area roads")
    };
    cout << "Local risk overrides applied:\n";
    for (size_t i = 0; i < sizeof(overrides) / sizeof(overrides[0]); i++) {
        graph.setRisksForRoadsTouching(overrides[i].getLocation(), overrides[i].getRisk(),
                                       overrides[i].getReason());
    }
}

// ---------- Result of one route search ----------
class RouteResult {
public:
    vector<int> path;
    double totalDistance;
    double riskExposure;   // sum of (risk * distance)
    bool found;

    RouteResult() : totalDistance(0), riskExposure(0), found(false) {}
    double averageRisk() const {
        return totalDistance > 0 ? riskExposure / totalDistance : 0;
    }
};

// ---------- Dijkstra with a min-heap ----------
class RouteFinder {
private:
    const Graph& graph;
    double alpha;        // weight given to distance
    double beta;         // weight given to risk
    double nightFactor;  // risk multiplier at night

    double effectiveRisk(double risk) const {
        return min(10.0, risk * nightFactor);
    }
    double edgeCost(const Road& r) const {
        return r.getDistance() * (alpha + beta * effectiveRisk(r.getRisk()));
    }
public:
    RouteFinder(const Graph& g, double alpha, double beta, bool isNight)
        : graph(g), alpha(alpha), beta(beta), nightFactor(isNight ? 1.5 : 1.0) {}

    RouteResult findRoute(int src, int dst) {             // yha se shuru hora dijstra
        int n = graph.size();
        const double INF = numeric_limits<double>::max();
        vector<double> dist(n, INF);
        vector<int> parent(n, -1);

        priority_queue<pair<double, int>,   // gives the shortest 
                       vector<pair<double, int>>,
                       greater<pair<double, int>>> pq;

        dist[src] = 0;
        pq.push(make_pair(0.0, src));  //source city priority queu me add hoti jati hai 

        while (!pq.empty()) {       // Jab tak queue mein cities hain, Dijkstra chalega
            pair<double, int> top = pq.top();
            pq.pop();
            double d = top.first;
            int u = top.second;

            if (d > dist[u]) continue;   // old entry, skip
            if (u == dst) break;

            const vector<Road>& roads = graph.getRoadsFrom(u);  //adjacency list lag rhi 
            for (size_t i = 0; i < roads.size(); i++) {
                int v = roads[i].getTo();
                double newDist = dist[u] + edgeCost(roads[i]);
                if (newDist < dist[v]) {
                    dist[v] = newDist;
                    parent[v] = u;
                    pq.push(make_pair(newDist, v));
                }
            }
        }

        RouteResult result;
        if (dist[dst] == INF) return result;   // no route

        // rebuild the path from destination back to source
        for (int at = dst; at != -1; at = parent[at]) result.path.push_back(at);
        reverse(result.path.begin(), result.path.end());

        // calculate real distance and risk of that path
        for (size_t i = 0; i + 1 < result.path.size(); i++) {
            int u = result.path[i], v = result.path[i + 1];
            const vector<Road>& roads = graph.getRoadsFrom(u);
            for (size_t j = 0; j < roads.size(); j++) {
                if (roads[j].getTo() == v) {
                    result.totalDistance += roads[j].getDistance();
                    result.riskExposure += roads[j].getDistance() * effectiveRisk(roads[j].getRisk());
                    break;
                }
            }
        }
        result.found = true;
        return result;
    }
};

// ---------- Exports everything for the dashboard ----------
class DataExporter {
private:
    static int projectX(double longitude) {
        return static_cast<int>(lround(30.0 + (longitude - 73.8) * 740.0 / (77.0 - 73.8)));
    }
    static int projectY(double latitude) {
        return static_cast<int>(lround(30.0 + (32.6 - latitude) * 400.0 / (32.6 - 29.5)));
    }
    static void writeNumberArray(ostream& out, const vector<double>& values) {
        out << "[";
        for (size_t i = 0; i < values.size(); i++) {
            if (i > 0) out << ",";
            out << values[i];
        }
        out << "]";
    }
    static void writeParentArray(ostream& out, const vector<int>& values) {
        out << "[";
        for (size_t i = 0; i < values.size(); i++) {
            if (i > 0) out << ",";
            out << values[i];
        }
        out << "]";
    }
    static void writeSourceMode(const Graph& g, int source, double alpha, double beta,
                                bool night, ostream& out) {
        vector<int> parents(g.size(), -1);
        vector<double> distances(g.size(), 0.0);
        vector<double> risks(g.size(), 0.0);
        RouteFinder finder(g, alpha, beta, night);
        for (int destination = 0; destination < g.size(); destination++) {
            const RouteResult route = finder.findRoute(source, destination);
            if (!route.found) {
                throw runtime_error("Cannot export routes for a disconnected graph.");
            }
            if (route.path.size() > 1) {
                parents[destination] = route.path[route.path.size() - 2];
            } else {
                parents[destination] = source;
            }
            distances[destination] = route.totalDistance;
            risks[destination] = route.averageRisk();
        }
        out << "{\"parent\":";
        writeParentArray(out, parents);
        out << ",\"distance\":";
        writeNumberArray(out, distances);
        out << ",\"risk\":";
        writeNumberArray(out, risks);
        out << "}";
    }
    static void writeModeRoutes(const Graph& g, int source, ostream& out) {
        const char* modeNames[] = {"fastest", "balanced", "safest"};
        const double alphas[] = {1.0, 1.0, 0.1};
        const double betas[] = {0.0, 0.5, 1.0};
        for (int mode = 0; mode < 3; mode++) {
            if (mode > 0) out << ",";
            out << "\"" << modeNames[mode] << "\":{\"day\":";
            writeSourceMode(g, source, alphas[mode], betas[mode], false, out);
            out << ",\"night\":";
            writeSourceMode(g, source, alphas[mode], betas[mode], true, out);
            out << "}";
        }
    }
public:
    static void exportData(const Graph& g, const string& filename) {
        ostringstream data;
        data << fixed << setprecision(4);
        data << "const cityData = {\n\"locations\":[";
        const vector<Location>& locs = g.getLocations();
        for (size_t i = 0; i < locs.size(); i++) {
            if (i > 0) data << ",";
            data << "{\"id\":" << locs[i].getId()
                 << ",\"name\":\"" << locs[i].getName() << "\""
                 << ",\"lat\":" << locs[i].getLatitude()
                 << ",\"lon\":" << locs[i].getLongitude()
                 << ",\"tier\":" << locs[i].getPopulationTier()
                 << ",\"x\":" << projectX(locs[i].getLongitude())
                 << ",\"y\":" << projectY(locs[i].getLatitude())
                 << ",\"safe\":" << (locs[i].isSafeSpot() ? "true" : "false")
                 << ",\"tricity\":" << (locs[i].isTricity() ? "true" : "false")
                 << ",\"detailed\":" << (locs[i].isDetailed() ? "true" : "false") << "}";
        }
        data << "],\n\"roads\":[";
        const vector<Road>& roads = g.getAllRoads();
        for (size_t i = 0; i < roads.size(); i++) {
            if (i > 0) data << ",";
            data << "{\"from\":" << roads[i].getFrom()
                 << ",\"to\":" << roads[i].getTo()
                 << ",\"distance\":" << roads[i].getDistance()
                 << ",\"risk\":" << roads[i].getRisk() << "}";
        }
        data << "],\n\"routes\":{";
        for (int source = 0; source < g.size(); source++) {
            if (source > 0) data << ",";
            data << "\"" << source << "\":{";
            writeModeRoutes(g, source, data);
            data << "}";
        }
        data << "}\n};\n";
        ofstream out(filename.c_str());
        if (!out) throw runtime_error("Unable to open data export file: " + filename);
        out << data.str();
        if (!out) throw runtime_error("Unable to write data export file: " + filename);
        out.close();
    }
};

// ---------- Build the Punjab town database and automatic road network ----------
Graph buildCity() {
    Graph g;
    int id = 0;
    g.addLocation(Location(id++, "Abohar", 30.1453, 74.1993, 2));
    g.addLocation(Location(id++, "Ajnala", 31.8440, 74.7620, 3));
    g.addLocation(Location(id++, "Amloh", 30.6080, 76.2320, 3));
    g.addLocation(Location(id++, "Amritsar", 31.6340, 74.8723, 1, true));
    g.addLocation(Location(id++, "Anandpur Sahib", 31.2390, 76.5020, 2));
    g.addLocation(Location(id++, "Baba Bakala", 31.5440, 75.2550, 3));
    g.addLocation(Location(id++, "Bagha Purana", 30.6880, 75.0950, 3));
    g.addLocation(Location(id++, "Banga", 31.1880, 75.9950, 3));
    g.addLocation(Location(id++, "Barnala", 30.3782, 75.5469, 2));
    g.addLocation(Location(id++, "Batala", 31.8180, 75.2040, 2));
    g.addLocation(Location(id++, "Bathinda", 30.2110, 74.9455, 1, true));
    g.addLocation(Location(id++, "Bhawanigarh", 30.2730, 76.0390, 3));
    g.addLocation(Location(id++, "Budhlada", 29.9270, 75.5620, 3));
    g.addLocation(Location(id++, "Chandigarh", 30.7333, 76.7794, 1, true, true));
    g.addLocation(Location(id++, "Chandigarh University (Gharuan)", 30.7846, 76.5752, 2, false, true, true));
    g.addLocation(Location(id++, "Dera Baba Nanak", 32.0310, 75.0270, 3));
    g.addLocation(Location(id++, "Dhuri", 30.3680, 75.8670, 3));
    g.addLocation(Location(id++, "Dinanagar", 32.1360, 75.4720, 3));
    g.addLocation(Location(id++, "Dirba", 30.0720, 75.9960, 3));
    g.addLocation(Location(id++, "Faridkot", 30.6762, 74.7580, 2));
    g.addLocation(Location(id++, "Fatehgarh Sahib", 30.6430, 76.3970, 2));
    g.addLocation(Location(id++, "Fazilka", 30.4021, 74.0280, 2));
    g.addLocation(Location(id++, "Ferozepur", 30.9331, 74.6225, 2));
    g.addLocation(Location(id++, "Garhshankar", 31.2150, 76.1420, 3));
    g.addLocation(Location(id++, "Gidderbaha", 30.2030, 74.6660, 3));
    g.addLocation(Location(id++, "Gurdaspur", 32.0414, 75.4031, 2));
    g.addLocation(Location(id++, "Hariana", 31.6330, 75.8380, 3));
    g.addLocation(Location(id++, "Hoshiarpur", 31.5143, 75.9115, 2));
    g.addLocation(Location(id++, "Jagraon", 30.7870, 75.4730, 2));
    g.addLocation(Location(id++, "Jalalabad", 30.6090, 74.2530, 3));
    g.addLocation(Location(id++, "Jalandhar", 31.3260, 75.5762, 1));
    g.addLocation(Location(id++, "Kapurthala", 31.3794, 75.3847, 2));
    g.addLocation(Location(id++, "Kharar", 30.7460, 76.6470, 2, false, true));
    g.addLocation(Location(id++, "Khanna", 30.7040, 76.2230, 2));
    g.addLocation(Location(id++, "Kotkapura", 30.5810, 74.8200, 3));
    g.addLocation(Location(id++, "Lehragaga", 29.9360, 75.8010, 3));
    g.addLocation(Location(id++, "Longowal", 30.2100, 75.6860, 3));
    g.addLocation(Location(id++, "Ludhiana", 30.9010, 75.8573, 1, true));
    g.addLocation(Location(id++, "Machhiwara", 30.9150, 76.2010, 3));
    g.addLocation(Location(id++, "Malerkotla", 30.5300, 75.8800, 2));
    g.addLocation(Location(id++, "Malout", 30.2110, 74.4810, 2));
    g.addLocation(Location(id++, "Mandi Gobindgarh", 30.6610, 76.3000, 2));
    g.addLocation(Location(id++, "Mansa", 29.9995, 75.3937, 2));
    g.addLocation(Location(id++, "Moga", 30.8165, 75.1711, 2));
    g.addLocation(Location(id++, "Mohali", 30.7046, 76.7179, 1, false, true));
    g.addLocation(Location(id++, "Morinda", 30.7890, 76.4980, 3));
    g.addLocation(Location(id++, "Muktsar", 30.4749, 74.5160, 2));
    g.addLocation(Location(id++, "Nabha", 30.3750, 76.1520, 2));
    g.addLocation(Location(id++, "Nakodar", 31.1254, 75.4750, 3));
    g.addLocation(Location(id++, "Nangal", 31.3750, 76.3760, 2));
    g.addLocation(Location(id++, "Nawanshahr", 31.1254, 76.1164, 2));
    g.addLocation(Location(id++, "Nurmahal", 31.0970, 75.5940, 3));
    g.addLocation(Location(id++, "Pathankot", 32.2643, 75.6421, 2));
    g.addLocation(Location(id++, "Patiala", 30.3398, 76.3869, 1, true));
    g.addLocation(Location(id++, "Patti", 31.2810, 74.8580, 3));
    g.addLocation(Location(id++, "Phagwara", 31.2240, 75.7690, 2));
    g.addLocation(Location(id++, "Phillaur", 31.0190, 75.7880, 3));
    g.addLocation(Location(id++, "Qadian", 31.7200, 75.3860, 3));
    g.addLocation(Location(id++, "Rajpura", 30.4840, 76.5940, 2));
    g.addLocation(Location(id++, "Rampura Phul", 30.2700, 75.2400, 3));
    g.addLocation(Location(id++, "Raikot", 30.6500, 75.6000, 3));
    g.addLocation(Location(id++, "Ropar", 30.9686, 76.5260, 2));
    g.addLocation(Location(id++, "Samana", 30.1590, 76.1930, 3));
    g.addLocation(Location(id++, "Samrala", 30.8360, 76.1930, 3));
    g.addLocation(Location(id++, "Sanaur", 30.3030, 76.4560, 3));
    g.addLocation(Location(id++, "Sangrur", 30.2458, 75.8421, 2));
    g.addLocation(Location(id++, "Sirhind", 30.6370, 76.3840, 2));
    g.addLocation(Location(id++, "Sri Hargobindpur", 31.6850, 75.4750, 3));
    g.addLocation(Location(id++, "Sunam", 30.1290, 75.7990, 2));
    g.addLocation(Location(id++, "Talwandi Sabo", 29.9860, 75.0850, 3));
    g.addLocation(Location(id++, "Tanda Urmar", 31.6730, 75.6370, 3));
    g.addLocation(Location(id++, "Tarn Taran", 31.4519, 74.9278, 2));
    g.addLocation(Location(id++, "Zira", 30.9700, 74.9900, 3));
    g.addLocation(Location(id++, "Zirakpur", 30.6420, 76.8170, 2, false, true));
    g.addLocation(Location(id++, "Chandigarh Sector 17 (City Centre Plaza)", 30.7408, 76.7827, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 22", 30.7332, 76.7673, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 34", 30.7180, 76.7570, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 35", 30.7127, 76.7622, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 43 (ISBT)", 30.7046, 76.7675, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 26 (Grain Market)", 30.7295, 76.8010, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 8", 30.7447, 76.7970, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 9", 30.7422, 76.8001, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 10", 30.7481, 76.7936, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 15", 30.7590, 76.7680, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 16", 30.7520, 76.7690, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 19", 30.7408, 76.7736, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 20", 30.7288, 76.7737, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 32", 30.7057, 76.7687, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 37", 30.7114, 76.7440, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 44", 30.6983, 76.7728, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 45", 30.6900, 76.7660, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 46", 30.6883, 76.7792, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 47", 30.6820, 76.7910, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Sector 48", 30.6733, 76.7880, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Industrial Area Phase 1", 30.7043, 76.8020, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Industrial Area Phase 2", 30.6917, 76.8074, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Chandigarh Railway Station", 30.6745, 76.8180, 1, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Shaheed Bhagat Singh International Airport (Mohali)", 30.6735, 76.7888, 1, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "PGI", 30.7649, 76.7756, 1, true, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "GMCH-32", 30.7047, 76.7686, 1, true, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Panjab University", 30.7600, 76.7684, 1, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Elante Mall", 30.7050, 76.8010, 1, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Sukhna Lake", 30.7421, 76.8185, 1, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Rock Garden", 30.7525, 76.8110, 1, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Tribune Chowk", 30.6963, 76.8017, 1, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Rajiv Gandhi IT Park", 30.7291, 76.8413, 1, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Panchkula", 30.6942, 76.8606, 1, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Mohali Phase 7", 30.6946, 76.7065, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Mohali Phase 8", 30.6897, 76.7028, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Mohali Sector 70", 30.6813, 76.7295, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Landran", 30.6458, 76.7077, 2, false, true, true)); // verify on Google Maps
    g.addLocation(Location(id++, "Mullanpur", 30.9100, 76.7380, 2, false, true, true)); // verify on Google Maps

    const char* explicitLocations[] = {
        "Chandigarh", "Chandigarh University (Gharuan)", "Kharar", "Mohali", "Zirakpur",
        "Chandigarh Sector 17 (City Centre Plaza)", "Chandigarh Sector 22",
        "Chandigarh Sector 34", "Chandigarh Sector 35", "Chandigarh Sector 43 (ISBT)",
        "Chandigarh Sector 26 (Grain Market)", "Chandigarh Sector 8", "Chandigarh Sector 9",
        "Chandigarh Sector 10", "Chandigarh Sector 15", "Chandigarh Sector 16",
        "Chandigarh Sector 19", "Chandigarh Sector 20", "Chandigarh Sector 32",
        "Chandigarh Sector 37", "Chandigarh Sector 44", "Chandigarh Sector 45",
        "Chandigarh Sector 46", "Chandigarh Sector 47", "Chandigarh Sector 48",
        "Industrial Area Phase 1", "Industrial Area Phase 2", "Chandigarh Railway Station",
        "Shaheed Bhagat Singh International Airport (Mohali)", "PGI", "GMCH-32",
        "Panjab University", "Elante Mall", "Sukhna Lake", "Rock Garden", "Tribune Chowk",
        "Rajiv Gandhi IT Park", "Panchkula", "Mohali Phase 7", "Mohali Phase 8",
        "Mohali Sector 70", "Landran", "Mullanpur"
    };
    for (size_t i = 0; i < sizeof(explicitLocations) / sizeof(explicitLocations[0]); i++) {
        g.markExplicitNetworkLocation(explicitLocations[i]);
    }

    g.connectNearestTowns();
    g.addHighwayRoad("Amritsar", "Jalandhar");
    g.addHighwayRoad("Jalandhar", "Phagwara");
    g.addHighwayRoad("Phagwara", "Ludhiana");
    g.addHighwayRoad("Ludhiana", "Khanna");
    g.addHighwayRoad("Khanna", "Sirhind");
    g.addHighwayRoad("Khanna", "Fatehgarh Sahib");
    g.addHighwayRoad("Sirhind", "Mohali");
    g.addHighwayRoad("Fatehgarh Sahib", "Sirhind");
    g.addHighwayRoad("Mohali", "Chandigarh");
    g.addHighwayRoad("Chandigarh", "Zirakpur");
    g.addHighwayRoad("Zirakpur", "Rajpura");
    g.addHighwayRoad("Rajpura", "Patiala");
    g.addHighwayRoad("Ludhiana", "Moga");
    g.addHighwayRoad("Moga", "Bathinda");
    g.addHighwayRoad("Bathinda", "Patiala");
    g.addHighwayRoad("Amritsar", "Batala");
    g.addHighwayRoad("Batala", "Gurdaspur");
    g.addHighwayRoad("Gurdaspur", "Pathankot");
    g.addHighwayRoad("Kharar", "Mohali");
    g.addHighwayRoad("Kharar", "Chandigarh University (Gharuan)");

    // Madhya Marg and Sukhna Lake road.
    g.addHighwayRoad("Chandigarh Sector 26 (Grain Market)", "Rajiv Gandhi IT Park");
    g.addHighwayRoad("Rajiv Gandhi IT Park", "Sukhna Lake");
    g.addHighwayRoad("Sukhna Lake", "Panchkula");
    g.addLocalRoad("Sukhna Lake", "Rock Garden");
    g.addLocalRoad("Rock Garden", "PGI");
    g.addLocalRoad("PGI", "Chandigarh Sector 10");
    g.addLocalRoad("Chandigarh Sector 10", "Chandigarh Sector 9");
    g.addLocalRoad("Chandigarh Sector 9", "Chandigarh Sector 8");
    g.addLocalRoad("Chandigarh Sector 8", "Chandigarh Sector 26 (Grain Market)");
    g.addLocalRoad("PGI", "Mullanpur");

    // Jan Marg, Himalaya Marg, and the internal sector grid.
    g.addHighwayRoad("Chandigarh Sector 17 (City Centre Plaza)", "Chandigarh Sector 22");
    g.addHighwayRoad("Chandigarh Sector 22", "Chandigarh Sector 35");
    g.addHighwayRoad("Chandigarh Sector 35", "Chandigarh Sector 43 (ISBT)");
    g.addLocalRoad("Chandigarh Sector 22", "Chandigarh Sector 15");
    g.addLocalRoad("Chandigarh Sector 15", "Chandigarh Sector 16");
    g.addLocalRoad("Chandigarh Sector 16", "Panjab University");
    g.addLocalRoad("Panjab University", "Chandigarh Sector 17 (City Centre Plaza)");
    g.addLocalRoad("Chandigarh Sector 17 (City Centre Plaza)", "Chandigarh Sector 19");
    g.addLocalRoad("Chandigarh Sector 19", "Chandigarh Sector 20");
    g.addLocalRoad("Chandigarh Sector 20", "Chandigarh Sector 34");
    g.addLocalRoad("Chandigarh Sector 34", "Chandigarh Sector 35");
    g.addLocalRoad("Chandigarh Sector 34", "Chandigarh Sector 37");
    g.addLocalRoad("Chandigarh Sector 35", "Chandigarh Sector 37");
    g.addLocalRoad("Chandigarh Sector 43 (ISBT)", "Chandigarh Sector 44");
    g.addLocalRoad("Chandigarh Sector 44", "Chandigarh Sector 45");
    g.addLocalRoad("Chandigarh Sector 45", "Chandigarh Sector 46");
    g.addLocalRoad("Chandigarh Sector 46", "Chandigarh Sector 47");
    g.addLocalRoad("Chandigarh Sector 47", "Chandigarh Sector 48");
    g.addLocalRoad("Chandigarh Sector 43 (ISBT)", "Chandigarh Sector 32");
    g.addLocalRoad("Chandigarh Sector 32", "GMCH-32");
    g.addLocalRoad("GMCH-32", "Industrial Area Phase 1");
    g.addLocalRoad("Industrial Area Phase 1", "Industrial Area Phase 2");
    g.addLocalRoad("Industrial Area Phase 2", "Tribune Chowk");
    g.addLocalRoad("Chandigarh Sector 26 (Grain Market)", "Chandigarh Sector 19");

    // Dakshin Marg arterial corridor.
    g.addHighwayRoad("Chandigarh Sector 43 (ISBT)", "Chandigarh Sector 44");
    g.addHighwayRoad("Chandigarh Sector 44", "Chandigarh Sector 45");

    // Udyog Path and the Chandigarh-Zirakpur NH5 corridor.
    g.addLocalRoad("Chandigarh Sector 26 (Grain Market)", "Chandigarh Railway Station");
    g.addLocalRoad("Chandigarh Railway Station", "Elante Mall");
    g.addLocalRoad("Elante Mall", "Industrial Area Phase 1");
    g.addHighwayRoad("Chandigarh Sector 17 (City Centre Plaza)", "Tribune Chowk");
    g.addHighwayRoad("Tribune Chowk", "Zirakpur");

    // Chandigarh-Mohali, airport road, and Kharar-Landran-Mohali roads.
    g.addHighwayRoad("Chandigarh Sector 43 (ISBT)", "Mohali");
    g.addHighwayRoad("Mohali", "Mohali Phase 7");
    g.addLocalRoad("Mohali Phase 7", "Mohali Phase 8");
    g.addHighwayRoad("Mohali Phase 8", "Shaheed Bhagat Singh International Airport (Mohali)");
    g.addHighwayRoad("Tribune Chowk", "Shaheed Bhagat Singh International Airport (Mohali)");
    g.addHighwayRoad("Kharar", "Landran");
    g.addHighwayRoad("Landran", "Mohali Sector 70");
    g.addLocalRoad("Mohali Sector 70", "Mohali Phase 7");
    g.addHighwayRoad("Kharar", "Mohali");
    g.addHighwayRoad("Kharar", "Chandigarh Sector 43 (ISBT)");

    g.ensureConnected();
    if (!g.isConnected()) {
        throw runtime_error("The road graph is not connected after explicit and Punjab links.");
    }
    cout << "Graph connectivity (BFS): connected\n";
    RiskModel::applyLocalKnowledge(g);
    return g;
}

void printRoutesForTrip(const Graph& city, const string& sourceName,
                        const string& destinationName) {
    const int source = city.findLocationId(sourceName);
    const int destination = city.findLocationId(destinationName);
    if (source < 0 || destination < 0) {
        throw runtime_error("Requested route references an unknown location.");
    }
    const char* modeNames[] = {"Fastest", "Balanced", "Safest"};
    const double alphas[] = {1.0, 1.0, 0.1};
    const double betas[] = {0.0, 0.5, 1.0};
    cout << "\nRoutes: " << sourceName << " -> " << destinationName << ", day\n\n";
    for (int mode = 0; mode < 3; mode++) {
        RouteFinder finder(city, alphas[mode], betas[mode], false);
        const RouteResult route = finder.findRoute(source, destination);
        cout << modeNames[mode] << ": ";
        for (size_t i = 0; i < route.path.size(); i++) {
            cout << city.getLocations()[route.path[i]].getName();
            if (i + 1 < route.path.size()) cout << " -> ";
        }
        cout << "\n   Distance: " << route.totalDistance << " km, Average risk: "
             << route.averageRisk() << "/10\n\n";
    }
}

// ---------- Main ----------
int main() {
    Graph city = buildCity();
    printRoutesForTrip(city, "Chandigarh Railway Station",
                       "Chandigarh University (Gharuan)");
    printRoutesForTrip(city, "Chandigarh Sector 17 (City Centre Plaza)",
                       "Shaheed Bhagat Singh International Airport (Mohali)");

    size_t dayDifferingPairs = 0;
    size_t nightDifferingPairs = 0;
    size_t totalPairs = 0;
    for (int s = 0; s < city.size(); s++) {
        for (int d = s + 1; d < city.size(); d++) {
            RouteFinder fastestDay(city, 1.0, 0.0, false);
            RouteFinder safestDay(city, 0.1, 1.0, false);
            if (fastestDay.findRoute(s, d).path != safestDay.findRoute(s, d).path) {
                dayDifferingPairs++;
            }
            RouteFinder fastestNight(city, 1.0, 0.0, true);
            RouteFinder safestNight(city, 0.1, 1.0, true);
            if (fastestNight.findRoute(s, d).path != safestNight.findRoute(s, d).path) {
                nightDifferingPairs++;
            }
            totalPairs++;
        }
    }
    const double dayDifferingPercentage = totalPairs == 0 ? 0.0
        : 100.0 * static_cast<double>(dayDifferingPairs) / totalPairs;
    const double nightDifferingPercentage = totalPairs == 0 ? 0.0
        : 100.0 * static_cast<double>(nightDifferingPairs) / totalPairs;
    cout << "Fastest/Safest differing town pairs: day "
         << fixed << setprecision(1) << dayDifferingPercentage << "% ("
         << dayDifferingPairs << "/" << totalPairs << "), night "
         << nightDifferingPercentage << "% (" << nightDifferingPairs
         << "/" << totalPairs << ")\n";

    DataExporter::exportData(city, "data.js");
    ifstream generated("data.js", ios::binary | ios::ate);
    if (!generated) throw runtime_error("Unable to measure generated data.js.");
    cout << "Locations: " << city.size() << "\n";
    cout << "Roads: " << city.getAllRoads().size() << "\n";
    cout << "data.js size: " << generated.tellg() << " bytes\n";
    cout << "data.js created for the dashboard.\n";
    return 0;
}