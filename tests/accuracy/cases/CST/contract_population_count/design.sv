// Authored independently for the accuracy suite.
// Environment: d launches on core; input arrival is [0,2] ns; q required [0,2] ns.
module top(input logic clk, input logic [3:0] d, output logic [2:0] q);

always_ff @(posedge clk) begin q <= {2'b0,d[0]} + {2'b0,d[1]} + {2'b0,d[2]} + {2'b0,d[3]}; end
endmodule
