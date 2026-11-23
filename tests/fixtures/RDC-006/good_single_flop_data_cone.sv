// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A single resettable register whose D cone has only primary inputs (no predecessor stage)
// is reported by RDC-003 as unrecognised, not RDC-006 as inter-stage logic.
// expect: RDC-003 WARNING
// expect-none: RDC-006
module top(input logic clka, rst_n, d, en, x, output logic q);
  logic r_gen;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) r_gen <= 1'b0; else r_gen <= d & en;
  always_ff @(posedge clka or negedge r_gen) if (!r_gen) q <= 1'b0; else q <= x;
endmodule
