// sverity-args: --top top --sdc @DIR@/gen.sdc
// revert: always_ff @(posedge div or negedge rst_n) if (!rst_n) q => always_ff @(posedge clka or negedge rst_n) if (!rst_n) q
// revert-expect-none: CDC-030 WARNING
// expect: CDC-030 WARNING
module top(input logic clka, rst_n, d, output logic q);
  logic div, gq;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) div <= 1'b0; else div <= ~div;
  always_ff @(posedge div or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
