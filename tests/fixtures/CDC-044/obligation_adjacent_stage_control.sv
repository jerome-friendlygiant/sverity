// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: else if (s3 & ~s4) cnt <= cnt + 1; => else cnt <= cnt;
// revert-expect-none: CDC-044 WARNING
// expect: CDC-044 WARNING
module top(input logic clka, rst_n, rx, output logic [7:0] cnt);
  logic s1, s2, s3, s4;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) {s4, s3, s2, s1} <= '0;
    else {s4, s3, s2, s1} <= {s3, s2, s1, rx};
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) cnt <= '0;
    else if (s3 & ~s4) cnt <= cnt + 1;
endmodule
