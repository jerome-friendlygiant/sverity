// sverity-args: --top top
// Every item of the case and its default return a value, so the return value is established on every path.
// expect-none: CST-010
module top(input logic clk, input logic [1:0] sel, output logic [1:0] q);
  function automatic logic [1:0] decode(input logic [1:0] s);
    unique case (s)
      2'd0:    return 2'd3;
      2'd1:    return 2'd2;
      default: return 2'd0;
    endcase
  endfunction
  always_ff @(posedge clk) q <= decode(sel);
endmodule
