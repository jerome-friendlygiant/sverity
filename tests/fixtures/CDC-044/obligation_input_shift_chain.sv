// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: {s4, s3, s2, s1} <= {s3, s2, s1, rx}; => {s4, s3, s2, s1} <= {s3, s2, s1, 1'b0};
// revert-expect-none: CDC-044 WARNING
// An asynchronous input captured into a four-stage shift chain (deserialiser style) is a stream: obligation, never "safe".
// expect: CDC-044 WARNING
module top(input logic clka, rst_n, rx, output logic [3:0] w);
  logic s1, s2, s3, s4;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {s4, s3, s2, s1} <= '0; else {s4, s3, s2, s1} <= {s3, s2, s1, rx};
  assign w = {s4, s3, s2, s1};
endmodule
