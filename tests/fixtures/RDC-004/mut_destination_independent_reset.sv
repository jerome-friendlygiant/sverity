// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: RDC-004 WARNING
// revert: always_ff @(posedge clka or negedge rst_b) if (!rst_b) q <= 1'b0; else q <= s; => always_ff @(posedge clka or negedge rst_a) if (!rst_a) q <= 1'b0; else q <= s;
// revert-expect-none: RDC-004 WARNING
module top(input logic clka, rst_a, rst_b, d, output logic q);
  logic s;
  always_ff @(posedge clka or negedge rst_a) if (!rst_a) s <= 1'b0; else s <= d;
  always_ff @(posedge clka or negedge rst_b) if (!rst_b) q <= 1'b0; else q <= s;
endmodule
