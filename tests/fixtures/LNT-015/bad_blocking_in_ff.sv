// sverity-args: --top top
// expect: LNT-015 WARNING 6
// expect: LNT-015 WARNING 9
module top(input logic clk, a, output logic q, r);
  logic c;
  always @(posedge clk) q = a;            // blocking in sequential block (legacy always)
  always_comb begin
    c = a;
    r <= c;                                // nonblocking in always_comb
  end
endmodule
