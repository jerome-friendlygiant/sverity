// sverity-args: --top top
// Lookalike of good_return_value_set_by_every_case_arm: without a default the case may match nothing, so no value is returned on that path.
// expect: CST-010 WARNING
module top(input logic clk, input logic [1:0] sel, output logic [1:0] q);
  function automatic logic [1:0] decode(input logic [1:0] s);
    case (s)
      2'd0: return 2'd3;
      2'd1: return 2'd2;
    endcase
  endfunction
  always_ff @(posedge clk) q <= decode(sel);
endmodule
