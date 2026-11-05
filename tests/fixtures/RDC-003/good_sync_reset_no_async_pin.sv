// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Near miss of bad_top_reset_unsynchronised.sv: the primary reset is applied synchronously, so it drives no asynchronous reset pin and no unsynchronised reset release is involved.
// expect-none: RDC-003
module top(input logic clka, arst_n, d, output logic q);
  always_ff @(posedge clka) if (!arst_n) q <= 1'b0; else q <= d;
endmodule
