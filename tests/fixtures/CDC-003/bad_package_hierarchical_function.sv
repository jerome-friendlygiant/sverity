// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-003 ERROR
// revert: return top.producer.state; => return 1'b0;
// revert-expect-none: CDC-003 ERROR
package accessors;
  function automatic logic read_state(); return top.producer.state; endfunction
endpackage
module producer_state(input logic clk, value);
  logic state;
  always_ff @(posedge clk) state <= value;
endmodule
module top(input logic clka, clkb, value, output logic result);
  producer_state producer(clka, value);
  always_ff @(posedge clkb) result <= accessors::read_state();
endmodule
