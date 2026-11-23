// sverity-args: --top top --sdc @FIXTURES@/CDC-047/primary_input_exclusive.sdc
// expect: CDC-008 INFO
module top(input logic clka, clkb, d, output logic q);
  logic s1;
  always_ff @(posedge clkb) begin
    s1 <= ~d;
    q <= s1;
  end
endmodule
