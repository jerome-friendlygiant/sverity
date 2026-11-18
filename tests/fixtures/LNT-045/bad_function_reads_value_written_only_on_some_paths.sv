// sverity-args: --top top
// Lookalike of the block that writes first: the write is conditional, so on other activations the function reads `t` from before the
// activation and the list lacks it.
// expect: LNT-045 WARNING 8
module top(input logic [3:0] a, output logic [3:0] y);
  logic [3:0] t;
  function automatic logic [3:0] f(); return t + 4'd1; endfunction
  always @(a) begin if (a[0]) t = a; y = f(); end
endmodule
