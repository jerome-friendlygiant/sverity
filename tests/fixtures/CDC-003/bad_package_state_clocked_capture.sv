// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-003 ERROR
package state;
  logic value;
  function automatic void put(input logic d);
    value = d;
  endfunction
endpackage
module top(input logic clka, clkb, d, output logic q);
  logic source;
  always_ff @(posedge clka) source <= d;
  always_ff @(posedge clkb) state::put(source);
  assign q = state::value;
endmodule
