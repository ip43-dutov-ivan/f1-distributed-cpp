#pragma once

enum class Kind {
	Pentagon
};

enum class With {
	TangentMidpoints
};

struct AroundRequest {
	Kind kind = Kind::Pentagon;
	With with = With::TangentMidpoints;
};

