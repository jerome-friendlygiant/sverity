// sverity-args: --top top --sdc @DIR@/primary_input.sdc
// revert: illegal_state <= (sync_b == 2'b11); => illegal_state <= 1'b0;
// revert-expect-none: CDC-010
// Two asynchronous input bits are independently synchronized, then treated as one coherent state.
// expect: CDC-010 WARNING
module top(input logic clka, clkb, input logic [1:0] state_async, output logic illegal_state);
  logic [1:0] sync_a, sync_b;
  always_ff @(posedge clkb) begin
    sync_a <= state_async;
    sync_b <= sync_a;
    illegal_state <= (sync_b == 2'b11);
  end
endmodule
