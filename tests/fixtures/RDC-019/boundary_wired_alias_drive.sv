// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The wand net rna is also held low by a constant: it is not a copy of rn, so the reset does not reach y as data (y sees a constant).
// expect-none: RDC-019
module top(input logic clka, clkb, rn, d, output logic q, y);
  wand rna;
  assign rna = rn;
  assign rna = 1'b0;
  always_ff @(posedge clka or negedge rn) if (!rn) q <= 1'b0; else q <= d;
  always_ff @(posedge clkb) y <= rna;
endmodule
