// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Looks undecidable: the strobe register is clocked by gclk, the never-driven output of an empty module, so no clock is identified for it. The rule still decides: strobe is a non-synchroniser register output driving async reset pins, so RDC-014 WARNING holds.
// expect: RDC-014 WARNING
// expect-count: RDC-014 WARNING 1
module ext_clk(output logic clk);
endmodule
module top(input logic clka, clkb, req, d, output logic q);
  logic gclk, strobe;
  ext_clk u_clk(.clk(gclk));
  always_ff @(posedge gclk) strobe <= req;
  always_ff @(posedge clkb or negedge strobe) if (!strobe) q <= 1'b0; else q <= d;
endmodule
