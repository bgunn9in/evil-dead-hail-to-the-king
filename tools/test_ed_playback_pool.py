from pathlib import Path
import unittest
from ed_playback_pool import GlobalPool,Clip,PoolFull,UndefinedSourceState,prove_source

A=(0,0);B=(4,0);C=(2,0)

class GlobalPoolTests(unittest.TestCase):
    def test_overwrite_preserves_waiting_child_parent(self):
        pool=GlobalPool();pool.add_actor((0,0))
        parent=pool.allocate((0,0));child=pool.link(parent,Clip())
        pool.overwrite(child,Clip(),0)
        self.assertEqual(pool.slots[child].parent,parent)
        self.assertEqual(pool.slots[parent].child,child)

    def test_disabled_child_link_survives_parent_and_is_not_reactivated(self):
        pool=GlobalPool();pool.add_actor(A)
        parent=pool.allocate(A,Clip(end=0,finish_start=0));child=pool.link(parent,Clip())
        pool.disable(child);before=pool.snapshot()
        self.assertFalse(pool.slots[child].active)
        self.assertEqual(pool.slots[parent].child,child)
        self.assertEqual(pool.tick(),[(A,parent,0)])
        self.assertFalse(pool.slots[child].active)
        self.assertEqual(pool.slots[child].mode,0)
        self.assertEqual(pool.slots[parent].child,child)
        self.assertEqual(pool.slots[child].parent,parent)
        self.assertTrue(pool.slots[parent].reusable)
        self.assertFalse(before==pool.snapshot())

    def pool(self):
        p=GlobalPool()
        for owner in(A,B,C):p.add_actor(owner)
        return p

    def test_first_free_and_actor_specific_priority(self):
        p=self.pool();self.assertEqual((p.allocate(A),p.allocate(B)),(0,1))
        self.assertEqual((p.allocate(A,priority=True),p.allocate(B,priority=True),p.allocate(A,priority=True)),(25,26,27))
        p.disable(25);self.assertEqual(p.allocate(A,priority=True),28)
        p.clear_actor(A);self.assertEqual(p.allocate(A,priority=True),25)

    def test_full_is_bounded_atomic_even_when_low_half_is_free(self):
        p=self.pool()
        for n in range(25):self.assertEqual(p.allocate(A,priority=True),25+n)
        before=p.snapshot()
        with self.assertRaises(PoolFull):p.allocate(A,priority=True)
        self.assertEqual(p.snapshot(),before);self.assertEqual(p.allocate(B),0)
        for _ in range(24):p.allocate(B)
        before=p.snapshot()
        with self.assertRaises(PoolFull):p.allocate(C)
        self.assertEqual(p.snapshot(),before)

    def test_child_later_dispatches_same_tick_across_actors(self):
        p=self.pool();a=p.allocate(A,Clip(end=0,finish_start=0));b=p.allocate(B,Clip(),2)
        child=p.link(a,Clip(end=1));self.assertEqual(child,2)
        self.assertEqual(p.tick(),[(A,0,0),(B,1,0),(A,2,0)])
        self.assertEqual(p.slots[child].frame,1)

    def test_child_earlier_waits_until_next_tick(self):
        p=self.pool();b=p.allocate(B,Clip(),2);a=p.allocate(A,Clip(end=0,finish_start=0));p.disable(b)
        child=p.link(a,Clip(end=1));self.assertEqual(child,0)
        self.assertEqual(p.tick(),[(A,1,0)]);self.assertEqual(p.slots[child].frame,0)
        self.assertEqual(p.tick(),[(A,0,0)])

    def test_reserved_leaf_not_reused_until_live_completion_pass(self):
        p=self.pool();p.allocate(A,Clip(end=0));p.allocate(B,Clip(end=0));p.tick()
        self.assertEqual(p.allocate(C,Clip(),2),2)
        reused=[]
        def restart(owner,index,pool):reused.append(pool.allocate(owner,Clip(),2))
        self.assertEqual(p.complete(restart),[(A,0,39),(B,1,39)]);self.assertEqual(reused,[0,1])
        self.assertEqual(p.slots[0].frame,0);self.assertEqual(p.slots[1].frame,0)

    def test_completion_uses_live_future_slot_state(self):
        p=self.pool();p.allocate(A,Clip(end=0));p.allocate(B,Clip(end=0));p.tick()
        def clear_future(owner,index,pool):
            if index==0:pool.disable(1)
        self.assertEqual(p.complete(clear_future),[(A,0,39)])

    def test_physical_unregister_does_not_release_or_gate_animation(self):
        p=self.pool();p.allocate(A,Clip(),2);p.unregister(A)
        self.assertEqual(p.tick(),[(A,0,0)])
        p.flags[A]=0;self.assertEqual(p.tick(),[]);self.assertTrue(p.slots[0].active)
        p.flags[A]=0x41;self.assertEqual(p.tick(),[]);self.assertTrue(p.slots[0].active)
        p.flags[A]=9;self.assertEqual(p.tick(),[(A,0,1)]) #Hidden8 ignored.

    def test_scene_clear_preserves_player_type_not_draw_registration(self):
        p=self.pool();p.allocate(A,Clip(),2);p.allocate(B,Clip(),2);p.unregister(A);p.clear_except_type(0)
        self.assertTrue(p.slots[0].active);self.assertIsNone(p.slots[1].owner);self.assertTrue(p.slots[1].reusable)

    def test_disable_has_no_end_event_overwrite_orders_old_end_new_begin(self):
        p=self.pool();i=p.allocate(A,Clip(begin_event=1,end_event=2));p.overwrite(i,Clip(begin_event=3,end_event=4),0)
        self.assertEqual(p.messages,[(A,0,1),(A,0,2),(A,0,3)]);p.disable(i);self.assertEqual(len(p.messages),3)

    def test_waiting_child_begin_event_is_allocation_time(self):
        p=self.pool();parent=p.allocate(A,Clip(end=0,finish_start=0));child=p.link(parent,Clip(begin_event=7))
        self.assertEqual(p.messages,[(A,child,7)]);self.assertEqual(p.slots[child].mode,4)

    def test_clear_actor_retains_source_ready_bit_and_exposes_invalid_midpass_use(self):
        p=self.pool();p.allocate(A,Clip(end=0));p.tick();p.clear_actor(A)
        with self.assertRaises(UndefinedSourceState):p.complete()
        p.clear_except_type(0);self.assertEqual(p.complete(),[])

    def test_priority_parent_child_is_allowed_to_allocate_in_low_half(self):
        p=self.pool();parent=p.allocate(A,Clip(end=0,finish_start=0),priority=True)
        child=p.link(parent,Clip());self.assertEqual((parent,child),(25,0))
        self.assertEqual(p.tick(),[(A,25,0)]);self.assertEqual(p.slots[child].frame,0)
        self.assertEqual(p.tick(),[(A,0,0)])

    def test_three_successors_finish_in_global_order(self):
        p=self.pool();first=p.allocate(A,Clip(end=0,finish_start=0));second=p.link(first,Clip(end=0,finish_start=0))
        third=p.link(second,Clip(end=1))
        self.assertEqual(p.tick(),[(A,first,0),(A,second,0),(A,third,0)])
        self.assertFalse(p.slots[first].active);self.assertFalse(p.slots[second].active);self.assertEqual(p.slots[third].frame,1)

    def test_cleanup_does_not_recheck_owner_active_or_frozen_flags(self):
        p=self.pool();p.allocate(A,Clip(end=0));p.tick();p.flags[A]=0x40
        self.assertEqual(p.complete(),[(A,0,39)])

    @unittest.skip("Retired PC resource fixture; Dreamcast-only project.")
    def test_original_pc_source_anchors(self):
        game=Path(__file__).resolve().parents[1]/"game/EvilDead.exe"
        if not game.exists():self.skipTest("Requires original PC copy")
        evidence=prove_source(game.read_bytes());self.assertEqual(evidence["capacity"],50)
        self.assertEqual(evidence["direct_call_sites"]["00405ad0"],[0x413DB8])

if __name__=="__main__":unittest.main()
