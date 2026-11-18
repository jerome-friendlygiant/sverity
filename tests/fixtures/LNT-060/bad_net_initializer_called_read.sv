// sverity-args: --top top
// expect: LNT-060 WARNING
module top(input logic clk, output wire y);
  logic q;
  function automatic logic get(); return q; endfunction
  wire w = get();
  assign y = w;
  always @(posedge clk) q = 1;
endmodule
