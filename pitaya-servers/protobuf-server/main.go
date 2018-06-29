package main

import (
	"flag"
	"fmt"
	"strings"

	"github.com/topfreegames/libpitaya/pitaya-servers/protobuf-server/services"
	"github.com/topfreegames/pitaya"
	"github.com/topfreegames/pitaya/acceptor"
	"github.com/topfreegames/pitaya/cluster"
	"github.com/topfreegames/pitaya/component"
	"github.com/topfreegames/pitaya/route"
	"github.com/topfreegames/pitaya/serialize/protobuf"
	"github.com/topfreegames/pitaya/session"
)

func configureFrontend(port int) {
	ws := acceptor.NewWSAcceptor(fmt.Sprintf(":%d", port))
	tcp := acceptor.NewTCPAcceptor(fmt.Sprintf(":%d", port+1))
	tls := acceptor.NewTCPAcceptor(fmt.Sprintf(":%d", port+2),
		"./fixtures/server/client-ssl.localhost.crt", "./fixtures/server/client-ssl.localhost.key")

	pitaya.Register(&services.Connector{},
		component.WithName("connector"),
		component.WithNameFunc(strings.ToLower),
	)

	err := pitaya.AddRoute("room", func(
		session *session.Session,
		route *route.Route,
		payload []byte,
		servers map[string]*cluster.Server,
	) (*cluster.Server, error) {
		// will return the first server
		for k := range servers {
			return servers[k], nil
		}
		return nil, nil
	})

	if err != nil {
		fmt.Printf("error adding route: %s\n", err.Error())
	}

	err = pitaya.SetDictionary(map[string]uint16{
		"connector.getsessiondata": 1,
		"connector.setsessiondata": 2,
	})

	pitaya.AddAcceptor(ws)
	pitaya.AddAcceptor(tcp)
	pitaya.AddAcceptor(tls)
}

func main() {
	port := flag.Int("port", 3350, "the port to listen")
	svType := flag.String("type", "connector", "the server type")

	flag.Parse()

	defer pitaya.Shutdown()

	ser := protobuf.NewSerializer()

	pitaya.SetSerializer(ser)

	configureFrontend(*port)

	pitaya.Configure(true, *svType, pitaya.Cluster, map[string]string{})
	pitaya.Start()
}