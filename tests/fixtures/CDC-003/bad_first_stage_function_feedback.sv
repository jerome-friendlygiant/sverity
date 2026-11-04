// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-003 ERROR
// revert: select_data(select_signal, launched, first) => launched
// revert-expect-none: CDC-003 ERROR
module top(input logic clka, clkb, select_signal, value, output logic result);
  logic launched, first, second;
  function automatic logic select_data(logic s, logic data, logic held);
    return s ? data : held;
  endfunction
  always_ff @(posedge clka) launched <= value;
  always_ff @(posedge clkb) begin
    first <= select_data(select_signal, launched, first);
    second <= first;
  end
  assign result = second;
endmodule
