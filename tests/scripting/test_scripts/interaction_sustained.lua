voxel.register_block({
    id = "test:grindstone",
    on_interact_start = function(pos, player)
        test_interact_started = true
        return true
    end,
    on_interact_step = function(pos, player, elapsed)
        test_interact_elapsed = elapsed
        if elapsed >= 3.0 then
            return false -- done
        end
        return true -- continue
    end,
    on_interact_stop = function(pos, player, elapsed)
        test_interact_stopped = true
        test_interact_stop_elapsed = elapsed
    end,
    on_interact_cancel = function(pos, player, elapsed, reason)
        test_cancel_reason = reason
        return true
    end,
})
