// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: always_ff @(posedge clka or posedge rst) if (rst) r <= 1'b0; else r <= d; => always_ff @(posedge clka or negedge rst) if (!rst) r <= 1'b0; else r <= d;
// revert-expect-none: RDC-028 ERROR
// expect: RDC-028 ERROR
module top(input logic clka, rst, d, output logic q, r);
  always_ff @(posedge clka or negedge rst) if (!rst) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or posedge rst) if (rst) r <= 1'b0; else r <= d;
endmodule
