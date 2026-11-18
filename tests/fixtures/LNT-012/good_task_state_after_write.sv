// sverity-args: --top top
// The task reads the state `s` it also writes, but the procedure assigns `s` before the call on every path: not feedback.
// expect-none: LNT-012
module top(input logic [3:0] x, output logic [3:0] s);
  task automatic acc(); s = s ^ x; endtask
  always_comb begin s = '0; acc(); end
endmodule
