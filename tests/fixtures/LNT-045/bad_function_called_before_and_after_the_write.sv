// sverity-args: --top top
// The second call sees the value the block wrote, but the first runs before the write and reads `t` from before the activation: the list lacks it.
// expect: LNT-045 WARNING 7
module top(input logic a, output logic y, z);
  logic t;
  function automatic logic f(); return t; endfunction
  always @(a) begin y = f(); t = a; z = f(); end
endmodule
