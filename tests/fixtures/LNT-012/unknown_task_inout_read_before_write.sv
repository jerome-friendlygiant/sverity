// sverity-args: --top top
// The task reads and updates its inout argument before the procedure has assigned `cnt`: the call may close a loop, which is not decided (Unresolved).
// expect: LNT-012 WARNING
// expect-none: LNT-012 ERROR
module top(input logic [3:0] x, output logic [3:0] cnt);
  task automatic inc(inout logic [3:0] v); v = v + x; endtask
  always_comb inc(cnt);
endmodule
