// sverity-args: --top top
// alias_clk is an exact wire copy of clk beside an extra untested edge event enable_n: clock inference is ambiguous,
// but rst_n is tested at top level so every edge event stays an async control (rst_n keeps its clear, others '?').
// expect: RDC-001 INFO
// expect: RDC-012 WARNING
// expect: RDC-013 WARNING
// expect-unresolved: RDC-026
module top(input logic clk, rst_n, enable_n, d, output logic q);
  wire alias_clk = clk;
  always @(posedge alias_clk or negedge rst_n or negedge enable_n)
    if (!rst_n) q <= 1'b0;
    else q <= d;
endmodule
