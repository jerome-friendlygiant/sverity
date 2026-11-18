// sverity-args: --top top
// The function reads `t`, which the block writes before the call in every activation: `t` is no input of the block.
// expect-none: LNT-045 WARNING
module top(input logic [3:0] a, output logic [3:0] y);
  logic [3:0] t;
  function automatic logic [3:0] f(); return t + 4'd1; endfunction
  always @(a) begin t = a; y = f(); end
endmodule
