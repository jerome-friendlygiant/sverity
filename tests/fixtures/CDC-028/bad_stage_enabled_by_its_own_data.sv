// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// `if (s) t <= s` can capture a rising s but never a falling one: it holds, so it is no synchroniser stage.
// revert: if (s) t <= s; => t <= s;
// revert-expect-none: CDC-028 ERROR
// expect: CDC-028 ERROR
// expect-none: CDC-003 INFO
module top(input logic clka, clkb, d, output logic z);
  logic s, t;
  always_ff @(posedge clka) s <= d;
  always_ff @(posedge clkb) begin
    if (s) t <= s;
    z <= t;
  end
endmodule
