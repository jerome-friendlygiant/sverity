// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-none: CDC-003 ERROR
// expect-none: CDC-005 ERROR
module top(input logic clka, clkb, d, y, output logic q, r);
  import "DPI-C" function bit g(input bit x);
  logic source_q, first_stage, second_stage;
  always_ff @(posedge clka) source_q <= d;
  always_ff @(posedge clkb) begin
    first_stage <= source_q;
    second_stage <= first_stage;
  end
  always_ff @(posedge clka) r <= g(y);
  assign q = second_stage;
endmodule
