// sverity-args: --top top --sdc @DIR@/unknown.sdc
// The generated clock's master name is not declared, so its relationship remains unresolved.
// expect: CDC-030 WARNING
// expect-unresolved: CDC-030
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clka, rst_n, d, output logic q);
  logic div;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) div <= 1'b0; else div <= ~div;
  always_ff @(posedge div or negedge rst_n)
    if (!rst_n) q <= 1'b0; else q <= d;
endmodule
