typedef enum {
	DRIFT_MESSAGE_TYPE_NONE,
	DRIFT_MESSAGE_TYPE_PLAYER_SPAWN,
	DRIFT_MESSAGE_TYPE_PLAYER_UPDATE,
} DriftMessageType;

typedef struct {
	u8 type;
	u16 size;
	u8 payload[];
} DriftMessage;

typedef struct {
	DriftMem* mem;
	DriftMessage* list[256];
	uint cursor;
	u8 buffer[64*1024];
} DriftMessages;

static void send_message(DriftMessages* messages, DriftMessageType type, void* payload, size_t size){
	DriftMessage* msg = DriftLinearMemAlloc(messages->mem, sizeof(*msg) + size);
	*msg = (DriftMessage){.type = type, .size = size};
	memcpy(msg->payload, payload, size);
	messages->list[messages->cursor++] = msg;
}

static DriftMessage* find_message(DriftMessages* messages, DriftMessageType type){
	DriftMessage** list = messages->list;
	for(uint i = 0; i < messages->cursor; i++){
		if(list[i]->type == type){
			list[i]->type = DRIFT_MESSAGE_TYPE_NONE;
			return list[i];
		}
	}
	
	return NULL;
}
