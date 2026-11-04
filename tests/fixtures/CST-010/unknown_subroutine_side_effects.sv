// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CST-010 WARNING
// expect-coverage: graph-precision INCOMPLETE
// expect: CDC-003 ERROR
module top(input logic clka, clkb, value, output logic task_result, function_result, side_result);
  logic remote;
  always_ff @(posedge clka) remote <= value;
  task automatic transfer(); task_result = remote; endtask
  function automatic logic transfer_value(); side_result = remote; return remote; endfunction
  always @(posedge clkb) begin transfer(); function_result <= transfer_value(); end
endmodule
