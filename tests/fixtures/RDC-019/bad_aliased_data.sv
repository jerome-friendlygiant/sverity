// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: always_ff @(posedge clka) y <= rna; => always_ff @(posedge clka) y <= d;
// revert-expect-none: RDC-019 WARNING
// The reset is copied through a wire and captured as data: an exact move carries the reset on to the functional use.
// expect: RDC-019 WARNING
module top(input logic clka, rn, d, output logic q, y);
  always_ff @(posedge clka or negedge rn) if (!rn) q <= 1'b0; else q <= d;
  wire rna = rn;
  always_ff @(posedge clka) y <= rna;
endmodule
