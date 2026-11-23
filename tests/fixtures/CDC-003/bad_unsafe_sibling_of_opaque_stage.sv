// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-003 ERROR
module top(input logic clka, clkb, d, output logic q);
  function automatic logic opaque_value(input logic value);
    static logic retained;
    retained = value;
    return retained;
  endfunction
  logic source_q, first_stage, second_stage, unsafe_capture;
  always_ff @(posedge clka) source_q <= d;
  always_ff @(posedge clkb) begin
    first_stage <= opaque_value(source_q);
    second_stage <= first_stage;
    unsafe_capture <= source_q;
  end
  assign q = second_stage | unsafe_capture;
endmodule
