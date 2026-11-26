// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// One control listed on both edges is one receiver: counted once (no two opposed controls), with an unknown forced value (CST-010).
// revert: always_ff @(posedge clka or negedge rst_n or posedge rst_n) => always_ff @(posedge clka or negedge rst_n)
// revert-expect-none: CST-010 WARNING
// expect-none: RDC-026 WARNING
// expect-unresolved: CST-010
module top(input logic clka, d, rst_n, output logic q);
  always_ff @(posedge clka or negedge rst_n or posedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
