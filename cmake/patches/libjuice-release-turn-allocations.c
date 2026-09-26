/*
 * NereusSDR change to libjuice 3c40a354 src/agent.c (MPL-2.0), inserted by
 * cmake/NereusRemoteMedia.cmake into a copy of agent.c in the build tree;
 * the fetched source is not modified. iPhone app plan Task 28 (R-IOS-16),
 * 2026-09-26, J.J. Boyd (KG4VCF), with AI-assisted implementation via
 * Anthropic Claude Code.
 *
 * When an agent is destroyed (its connection has ended), each TURN
 * allocation it made is given back with a Refresh whose LIFETIME is 0
 * (RFC 8656 section 7.2), so the relay's per-user quota is free again at
 * once instead of for the rest of the allocation's lifetime (TURN_LIFETIME,
 * 10 minutes). One datagram, not retransmitted: a lost one leaves the
 * allocation to expire as before.
 */
static void nereus_release_turn_allocations(juice_agent_t *agent) {
	if (!agent->conn_impl)
		return;

	conn_lock(agent);
	for (int i = 0; i < agent->entries_count; ++i) {
		agent_stun_entry_t *entry = agent->entries + i;
		if (entry->type != AGENT_STUN_ENTRY_TYPE_RELAY || !entry->turn || entry->relayed.len == 0)
			continue;
		if (entry->state != AGENT_STUN_ENTRY_STATE_SUCCEEDED &&
		    entry->state != AGENT_STUN_ENTRY_STATE_SUCCEEDED_KEEPALIVE)
			continue;
		if (*entry->turn->credentials.nonce == '\0')
			continue;

		stun_message_t msg;
		memset(&msg, 0, sizeof(msg));
		msg.msg_class = STUN_CLASS_REQUEST;
		msg.msg_method = STUN_METHOD_REFRESH;
		juice_random(msg.transaction_id, STUN_TRANSACTION_ID_SIZE);
		msg.lifetime = 0;
		msg.lifetime_set = true;
		msg.credentials = entry->turn->credentials;

		char buffer[BUFFER_SIZE];
		int size = stun_write(buffer, BUFFER_SIZE, &msg, entry->turn->password);
		if (size <= 0) {
			JLOG_WARN("TURN release message write failed");
			continue;
		}
		JLOG_DEBUG("Releasing TURN allocation");
		agent_direct_send(agent, &entry->record, buffer, size, 0);
	}
	conn_unlock(agent);
}

