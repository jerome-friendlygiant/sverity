// sverity-args: --top top
// The task of the interface is called only through a virtual interface, which this scan does not resolve to the instance: data is not reported as never driven.
// expect-none: LNT-008
interface bus_if;
  logic [7:0] data;
  task automatic put(input logic [7:0] x); data = x; endtask
endinterface
module top(input logic [7:0] v, output logic [7:0] y);
  bus_if bi();
  virtual bus_if vi;
  initial begin vi = bi; vi.put(v); end
  assign y = bi.data;
endmodule
