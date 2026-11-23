// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: start <= ({s4, s3} == 2'b10); => start <= s4;
// revert-expect-none: CDC-044 WARNING
// expect: CDC-044 WARNING
module top(input logic clka, rst_n, rx, output logic start);
  logic s1, s2, s3, s4;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) {s4, s3, s2, s1} <= '0;
    else {s4, s3, s2, s1} <= {s3, s2, s1, rx};
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) start <= 1'b0;
    else start <= ({s4, s3} == 2'b10);
endmodule
