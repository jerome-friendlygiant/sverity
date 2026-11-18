// sverity-args: --top top
// expect-none: LNT-060
module top(input logic clk);
  logic q;
  function automatic logic get(); return q; endfunction
  always @(posedge clk) q = 1;
  if (0) begin : inactive
    wire w = get();
  end
endmodule
