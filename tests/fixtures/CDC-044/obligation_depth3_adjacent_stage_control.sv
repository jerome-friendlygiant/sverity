// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: else if (s2 > s3) count <= count + 1; => else count <= count;
// revert-expect-none: CDC-044 WARNING
// expect: CDC-044 WARNING
module top(input logic clka, rst_n, rx, output logic [3:0] count);
  logic s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) {s3, s2, s1} <= '0;
    else {s3, s2, s1} <= {s2, s1, rx};
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) count <= '0;
    else if (s2 > s3) count <= count + 1;
endmodule
