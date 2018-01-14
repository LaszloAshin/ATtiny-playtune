#include <vector>
#include <iostream>

struct Sample {
	int value;
	int group;

	Sample(int value) : value(value) {}
};

struct Group {
	int value;
	int accumulator;
	int refcount;

	Group(int value) : value(value), accumulator(), refcount() {}
};

int
dist(const Group& g, const Sample& s)
{
	return (g.value - s.value) * (g.value - s.value);
}

int
main()
{
	srand(time(nullptr));
	std::vector<Sample> samples;
	for (int value; std::cin >> value;) {
		samples.push_back({value});
	}
	int min = samples.front().value;
	int max = samples.front().value;
	for (unsigned i = 1; i < samples.size(); ++i) {
		if (samples[i].value < min) min = samples[i].value;
		if (samples[i].value > max) max = samples[i].value;
	}
	std::vector<Group> groups;
	for (int i = 0; i < 5; ++i) {
		groups.push_back(min + rand() % (max + 1 - min));
	}
	std::cout << "~50 100 150 300 450" << std::endl;
	while (true) {
		bool motion = false;
		for (auto& s : samples) {
			int j = 0;
			for (unsigned i = 1; i < groups.size(); ++i) {
				if (dist(groups[i], s) < dist(groups[j], s)) {
					j = i;
				}
			}
			groups[j].accumulator += s.value;
			++groups[j].refcount;
			if (s.group != j) {
				motion = true;
				s.group = j;
			}
		}
		for (auto& g : groups) {
			std::cout << g.value << '(' << g.refcount << ") ";
			if (!g.refcount) {
				motion = true;
				g = Group(min + rand() % (max + 1 - min));
				continue;
			}
			g = Group(g.accumulator / g.refcount);
		}
		int totaldist = 0;
		int maxdist = 0;
		for (const auto& s : samples) {
			const auto d = dist(groups[s.group], s);
			totaldist += d;
			if (d > maxdist) maxdist = d;
		}
		std::cout << "sum(e)=" << totaldist << " max(e)=" << maxdist << std::endl;
		if (!motion) break;
	}
}
