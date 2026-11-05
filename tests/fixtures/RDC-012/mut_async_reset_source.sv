// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: RDC-012 WARNING
// revert: always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d; => always_ff @(posedge clka) if (rst_n) q <= 1'b0; else q <= d;
// revert-expect-none: RDC-012 WARNING
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
