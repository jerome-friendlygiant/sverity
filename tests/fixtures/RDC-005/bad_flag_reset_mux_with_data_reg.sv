// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The flag reset is a mux between a port and a clka data register g: a data leaf in the combinational reset source is not recognised.
// expect: RDC-005 WARNING
module top(input logic clka, clkb, rsta, sel, x, output logic ob);
  logic g, r, f, b1, b2;
  always_ff @(posedge clka or posedge rsta) if (rsta) g <= 1'b0; else g <= x;
  assign r = sel ? rsta : g;
  always_ff @(posedge clka or posedge r) if (r) f <= 1'b1; else f <= 1'b0;
  always_ff @(posedge clkb) begin b1 <= f; b2 <= b1; end
  assign ob = b2;
endmodule
