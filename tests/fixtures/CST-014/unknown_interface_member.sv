// sverity-args: --top top --sdc @DIR@/unknown_interface_member.sdc
// Interfaces are not followed, so a member that looks undriven may be driven through a modport.
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-028 ERROR
interface mode_if;
  logic m;
  modport writer(output m);
endinterface
module setter(mode_if.writer port);
  initial port.m = 1'b1;
endmodule
module top(input logic a, b, output logic q);
  mode_if bus();
  setter u(.port(bus));
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (bus.m) q <= source_q;
endmodule
