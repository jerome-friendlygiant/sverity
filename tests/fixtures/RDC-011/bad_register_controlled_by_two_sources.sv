// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// One register is cleared by either of two independent resets: the order in which they release decides its first value.
// expect: RDC-011 WARNING
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, rst_a_n, rst_b_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_a_n or negedge rst_b_n)
    if (!rst_a_n) q <= 1'b0;
    else if (!rst_b_n) q <= 1'b0;
    else q <= d;
endmodule
