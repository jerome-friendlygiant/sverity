// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: assign start = s3 & ~s2; => assign start = s3;
// revert-expect-none: CDC-044 WARNING
// A synchronised asynchronous input that is edge-detected is an event/serial stream: obligation, never "safe".
// expect: CDC-044 WARNING
module top(input logic clka, rst_n, rx, output logic start);
  logic s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {s3, s2, s1} <= '0; else {s3, s2, s1} <= {s2, s1, rx};
  assign start = s3 & ~s2;
endmodule
