// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A constant-D flag asynchronously reset by a data register g (not a recognised reset source) is a stretched data pulse, not exempt from RDC-005.
// expect: RDC-005 WARNING
module top(input logic clka, clkb, rsta, rstb, x, output logic ob);
  logic g, f, b1, b2;
  always_ff @(posedge clka or posedge rsta) if (rsta) g <= 1'b0; else g <= x;
  always_ff @(posedge clka or posedge g) if (g) f <= 1'b1; else f <= 1'b0;
  always_ff @(posedge clkb) begin b1 <= f; b2 <= b1; end
  assign ob = b2;
endmodule
